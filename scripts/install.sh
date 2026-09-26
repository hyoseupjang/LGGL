#!/bin/sh
set -eu

# CI fills these values when it builds the release.
main() {
    version='@VERSION@'
    base='https://github.com/@REPOSITORY@/releases/download/v@VERSION@'

    [ "$(id -u)" = 0 ] || { echo 'root로 실행하십시오.' >&2; exit 1; }
    command -v apk >/dev/null || { echo 'OpenWrt 25.12의 apk가 필요합니다.' >&2; exit 1; }
    arch=$(cat /etc/apk/arch)
    [ -n "$arch" ] || { echo '패키지 아키텍처를 확인할 수 없습니다.' >&2; exit 1; }
    case " @ARCHITECTURES@ " in
        *" $arch "*) ;;
        *) echo "이 릴리스에서 지원하지 않는 아키텍처입니다: $arch" >&2; exit 1 ;;
    esac

    install_dir=$(mktemp -d /tmp/lggl-install.XXXXXX)
    trap 'rm -rf "$install_dir"' EXIT
    trap 'exit 1' HUP INT TERM
    cd "$install_dir"
    for file in "lggl-$version.apk" "luci-app-lggl-$version.apk"; do
        wget -q "$base/${file%.apk}-$arch.apk" -O "$file"
    done
    apk update
    apk add --allow-untrusted "./lggl-$version.apk" "./luci-app-lggl-$version.apk" </dev/null
    /etc/init.d/lggl enable
    /etc/init.d/lggl restart
    echo '설치 완료. LuCI의 Services → LGGL에서 WAN 장치를 선택하고 Apply를 누르십시오.'
}

main "$@"
