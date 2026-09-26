#define _GNU_SOURCE
#include "protocol.h"
#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <linux/if_packet.h>
#include <net/if.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/sysinfo.h>
#include <syslog.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdarg.h>
#include <time.h>

/* Per-process fixed windows; debug traffic cannot consume the normal log budget. */
static int debug_logging;
static time_t log_window[2];
static unsigned log_count[2];
static void log_message(int priority,const char *format,...) {
    int saved_errno=errno, debug=priority==LOG_DEBUG;
    if(debug && !debug_logging) return;
    struct timespec now={0};
    clock_gettime(CLOCK_MONOTONIC,&now);
    unsigned limit=debug ? 100 : 10;
    if(now.tv_sec-log_window[debug]>=60) {
        log_window[debug]=now.tv_sec; log_count[debug]=0;
    }
    if(log_count[debug]>=limit) return;
    log_count[debug]++;
    char message[512]; va_list args;
    va_start(args,format); errno=saved_errno;
    vsnprintf(message,sizeof(message),format,args); va_end(args);
    syslog(priority,"%s",message);
    if(log_count[debug]==limit)
        syslog(LOG_NOTICE,"%s log limit reached; suppressing until next 60s window",
               debug ? "debug" : "normal");
    errno=saved_errno;
}

static volatile sig_atomic_t stopping;
static void stop(int sig) { (void)sig; stopping=1; }
static int valid_name(const char *s) {
    if(!*s || strlen(s)>=IFNAMSIZ || !strcmp(s,".") || !strcmp(s,"..")) return 0;
    for(;*s;s++) if(!isalnum((unsigned char)*s) && !strchr("_.:-",*s)) return 0;
    return 1;
}
static int open_socket(const char *name,uint16_t proto,int *index,uint8_t mac[6]) {
    int fd=socket(AF_PACKET,SOCK_DGRAM|SOCK_CLOEXEC|SOCK_NONBLOCK,htons(proto));
    if(fd<0) return -1;
    struct ifreq req={0}; snprintf(req.ifr_name,sizeof(req.ifr_name),"%s",name);
    if(ioctl(fd,SIOCGIFINDEX,&req)<0) goto fail;
    *index=req.ifr_ifindex;
    if(ioctl(fd,SIOCGIFHWADDR,&req)<0) goto fail;
    if(req.ifr_hwaddr.sa_family!=1) { errno=EINVAL; goto fail; }
    memcpy(mac,req.ifr_hwaddr.sa_data,6);
    struct sockaddr_ll addr={.sll_family=AF_PACKET,.sll_protocol=htons(proto),.sll_ifindex=*index};
    if(bind(fd,(struct sockaddr *)&addr,sizeof(addr))<0) goto fail;
    return fd;
fail: { int e=errno; close(fd); errno=e; return -1; }
}
static void close_sockets(struct pollfd p[2]) {
    for(int i=0;i<2;i++) {
        if(p[i].fd>=0) close(p[i].fd);
        p[i].fd=-1; p[i].revents=0;
    }
}
static void save_state(const char *path,const char *state,int error) {
    char tmp[160]; snprintf(tmp,sizeof(tmp),"%s.tmp",path);
    FILE *f=fopen(tmp,"w");
    if(!f) { log_message(LOG_ERR,"state file: %m"); return; }
    int ok=fprintf(f,"%ld %s %d\n",(long)getpid(),state,error)>=0;
    if(fclose(f)) ok=0;
    if(!ok || rename(tmp,path)) { unlink(tmp); log_message(LOG_ERR,"state write failed"); }
}
/* Called only after lggl_reply validated the DS header (at least 16 bytes). */
static uint32_t read_be32(const uint8_t *p) {
    return (uint32_t)p[0]<<24 | (uint32_t)p[1]<<16 | (uint32_t)p[2]<<8 | p[3];
}
static void log_ds(const char *name,const uint8_t *in,size_t len,const uint8_t src[6],
                   const uint8_t *out,size_t outlen,ssize_t sent,int error) {
    if(!debug_logging) return;
    uint32_t command=read_be32(in+8),id=read_be32(in+4),status=read_be32(out+12);
    const char *kind=command==201 ? "INFO" : command==202 ? "ACK" : "OTHER";
    char raw[48],detail[320];
    for(size_t n=0;n<16;n++) snprintf(raw+3*n,sizeof(raw)-3*n,n==15?"%02x":"%02x ",in[n]);
    snprintf(detail,sizeof(detail),
        "%s: DS %s src=%02x:%02x:%02x:%02x:%02x:%02x id_raw=0x%08"PRIx32
        " cmd=0x%08"PRIx32" (%"PRIu32") len=%zu raw16=[%s] response_status=%"PRIu32,
        name,kind,src[0],src[1],src[2],src[3],src[4],src[5],id,command,command,len,raw,status);
    if(sent==(ssize_t)outlen) log_message(LOG_DEBUG,"%s; sent %zu bytes",detail,outlen);
    else log_message(LOG_DEBUG,"%s; send failed: %s (sent=%zd expected=%zu)",
                detail,sent<0?strerror(error):"short send",sent,outlen);
}
static int save_counts(const char *path,const uint64_t count[2]) {
    char tmp[160]; snprintf(tmp,sizeof(tmp),"%s.tmp",path);
    FILE *f=fopen(tmp,"w"); if(!f) return -1;
    int ok=fprintf(f,"%"PRIu64" %"PRIu64"\n",count[0],count[1])>=0;
    if(fclose(f)) ok=0;
    if(!ok || rename(tmp,path)) { unlink(tmp); return -1; }
    return 0;
}
int main(int argc,char **argv) {
    if((argc!=2 && argc!=3) || !valid_name(argv[1]) ||
       (argc==3 && strcmp(argv[2],"--debug"))) {
        fprintf(stderr,"Usage: lggld DEVICE [--debug]\n"); return 2;
    }
    debug_logging=argc==3;
    const char *name=argv[1]; openlog("lggl",LOG_PID,LOG_DAEMON);
    umask(022);
    if(mkdir("/var/run/lggl",0755)<0 && errno!=EEXIST) return 1;
    char path[128],lockpath[128],statepath[128];
    snprintf(statepath,sizeof(statepath),"/var/run/lggl/%s.state",name);
    snprintf(path,sizeof(path),"/var/run/lggl/%s.counts",name);
    snprintf(lockpath,sizeof(lockpath),"/var/run/lggl/%s.lock",name);
    int lock=open(lockpath,O_CREAT|O_RDWR|O_CLOEXEC,0600);
    if(lock<0 || flock(lock,LOCK_EX|LOCK_NB)<0) { log_message(LOG_ERR,"%s: instance lock failed",name); return 1; }
    uint64_t counts[2]={0,0}; FILE *f=fopen(path,"r");
    if(f) { if(fscanf(f,"%"SCNu64" %"SCNu64,&counts[0],&counts[1])!=2) counts[0]=counts[1]=0; fclose(f); }
    struct sigaction sa={.sa_handler=stop}; sigemptyset(&sa.sa_mask);
    sigaction(SIGTERM,&sa,NULL); sigaction(SIGINT,&sa,NULL);
    const uint16_t proto[2]={0x0807,0x1200}; uint8_t mac[6]; int index=0;
    struct pollfd p[2]={{.fd=-1,.events=POLLIN},{.fd=-1,.events=POLLIN}};
    if(save_counts(path,counts)) log_message(LOG_ERR,"%s: counter write: %m",name);
    int waiting=0;
    while(!stopping) {
        if(p[0].fd<0) {
            int second_index=0;
            p[0].fd=open_socket(name,proto[0],&index,mac);
            if(p[0].fd>=0) p[1].fd=open_socket(name,proto[1],&second_index,mac);
            if(p[0].fd<0 || p[1].fd<0 || index!=second_index) {
                int error=errno;
                if(p[0].fd>=0 && p[1].fd>=0) error=ENODEV;
                close_sockets(p);
                if(!waiting) {
                    log_message(LOG_WARNING,"%s: sockets unavailable (%s); retrying",name,strerror(error));
                    save_state(statepath,"waiting",error);
                }
                waiting=1;
                poll(NULL,0,5000);
                continue;
            }
            waiting=0;
            save_state(statepath,"listening",0);
            log_message(LOG_INFO,"%s: listening for UB and DS",name);
        }
        int n=poll(p,2,1000);
        if(n<0) {
            if(errno==EINTR) continue;
            log_message(LOG_ERR,"%s: poll: %m",name);
            close_sockets(p);
            continue;
        }
        if(stopping) break;
        if((int)if_nametoindex(name)!=index) {
            log_message(LOG_NOTICE,"%s: device changed; reopening",name);
            save_state(statepath,"waiting",ENODEV);
            close_sockets(p);
            continue;
        }
        for(int i=0;i<2;i++) {
            if(p[i].revents&(POLLERR|POLLHUP|POLLNVAL)) {
                save_state(statepath,"waiting",ENODEV); close_sockets(p); break;
            }
            if(!(p[i].revents&POLLIN)) continue;
            uint8_t in[1500],out[283]; struct sockaddr_ll peer={0}; socklen_t plen=sizeof(peer);
            ssize_t len=recvfrom(p[i].fd,in,sizeof(in),MSG_TRUNC,(struct sockaddr *)&peer,&plen);
            if(len<0) { if(errno!=EINTR && errno!=EAGAIN && errno!=EWOULDBLOCK) log_message(LOG_ERR,"%s: recv: %m",name); continue; }
            /* Re-read the MAC for each request: netifd may change it in place. */
            struct ifreq current={0};
            snprintf(current.ifr_name,sizeof(current.ifr_name),"%s",name);
            if(ioctl(p[i].fd,SIOCGIFINDEX,&current)<0 || current.ifr_ifindex!=index ||
               ioctl(p[i].fd,SIOCGIFHWADDR,&current)<0) {
                save_state(statepath,"waiting",ENODEV); close_sockets(p); break;
            }
            memcpy(mac,current.ifr_hwaddr.sa_data,6);
            if((size_t)len>sizeof(in) || plen<sizeof(peer) || peer.sll_ifindex!=index ||
               peer.sll_pkttype==PACKET_OUTGOING || peer.sll_halen!=6 || (peer.sll_addr[0]&1) || !memcmp(peer.sll_addr,mac,6)) continue;
            struct sysinfo info={0}; sysinfo(&info); uint16_t tx=0;
            size_t outlen=lggl_reply(proto[i],in,len,mac,info.uptime,out,&tx);
            if(!outlen) continue;
            counts[i]++; if(save_counts(path,counts)) log_message(LOG_ERR,"%s: counter write: %m",name);
            peer.sll_family=AF_PACKET; peer.sll_protocol=htons(tx); peer.sll_ifindex=index; peer.sll_halen=6;
            ssize_t sent=sendto(p[i].fd,out,outlen,0,(struct sockaddr *)&peer,sizeof(peer));
            int send_error=errno;
            if(i) log_ds(name,in,(size_t)len,peer.sll_addr,out,outlen,sent,send_error);
            if(sent!=(ssize_t)outlen)
                log_message(LOG_ERR,"%s: %s reply failed: %s",name,i ? "DS" : "UB",
                            sent<0 ? strerror(send_error) : "short send");
            else if(!i) log_message(LOG_DEBUG,"%s: UB request received; sent %zu bytes",name,outlen);
        }
    }
    close_sockets(p); unlink(statepath); close(lock); return 0;
}
