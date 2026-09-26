'use strict';
'require view';
'require network';
'require uci';
'require rpc';
'require poll';
'require ui';

var getStatus = rpc.declare({ object: 'lggl', method: 'status' });

return view.extend({
    load: function() {
        return Promise.all([network.getDevices(), uci.load('lggl')]);
    },

    render: function(data) {
        var selected = L.toArray(uci.get('lggl', 'main', 'interfaces'));
        var names = data[0].filter(function(d) {
            return d.getName() !== 'lo' &&
                ['ethernet', 'switch', 'bridge', 'vlan'].includes(d.getType());
        }).map(function(d) { return d.getName(); });
        selected.forEach(function(name) {
            if (!names.includes(name)) names.push(name);
        });
        names.sort(L.naturalCompare);
        var checks = names.map(function(name) {
            return E('label', { style: 'display:block;margin:0.7em 0' }, [
                E('input', { type: 'checkbox', 'data-device': name,
                    checked: selected.includes(name) ? '' : null }),
                ' ' + name
            ]);
        });
        var list = E('div', {}, checks);
        var debug = E('input', { type: 'checkbox',
            checked: uci.get('lggl', 'main', 'debug') === '1' ? '' : null });
        var ub = E('span', {}, '…'), ds = E('span', {}, '…');
        var diagnostics = E('div', {}, 'Loading…');
        var update = function() {
            return getStatus().then(function(s) {
                ub.textContent = s.ub == null ? 'Unavailable' : String(s.ub);
                ds.textContent = s.ds == null ? 'Unavailable' : String(s.ds);
                var rows = (s.devices || []).map(function(d) {
                    var labels = { listening: 'Listening', waiting: 'Waiting for device / socket',
                        starting: 'Starting', stopped: 'Stopped' };
                    return E('p', {}, d.name + ': ' + (labels[d.state] || 'Unknown') +
                        (d.mac ? ' — ' + d.mac : '') +
                        (d.error ? ' (system error ' + d.error + ')' : ''));
                });
                diagnostics.replaceChildren.apply(diagnostics,
                    rows.length ? rows : [E('p', {}, 'No interfaces configured.')]);
            }).catch(function() {
                ub.textContent = ds.textContent = 'Unavailable';
                diagnostics.textContent = 'Status unavailable';
            });
        };
        var save = function() {
            var values = Array.from(list.querySelectorAll('input:checked'))
                .map(function(input) { return input.getAttribute('data-device'); });
            if (values.length) uci.set('lggl', 'main', 'interfaces', values);
            else uci.unset('lggl', 'main', 'interfaces');
            uci.set('lggl', 'main', 'debug', debug.checked ? '1' : '0');
            return uci.save().then(function() { return ui.changes.init(); });
        };
        poll.add(update, 5);
        update();
        return E('div', {}, [
            E('h2', {}, 'LGGL'),
            E('h3', {}, 'Interfaces to respond on'),
            E('p', {}, 'Select the upstream Ethernet device. Normally, this is the WAN interface.'),
            list,
            E('label', {}, [debug, ' Debug logging (default off, rate limited)']),
            E('div', { style: 'margin:1.5em 0' }, [
                E('button', { class: 'cbi-button cbi-button-save',
                    click: ui.createHandlerFn(this, save) }, 'Save'), ' ',
                E('button', { class: 'cbi-button cbi-button-apply',
                    click: ui.createHandlerFn(this, function() {
                        return save().then(function() { return ui.changes.apply(true); });
                    }) }, 'Apply')
            ]),
            E('h3', {}, 'Counters'),
            E('p', {}, 'Valid requests received since boot.'),
            E('p', {}, ['UB: ', ub]),
            E('p', {}, ['DS: ', ds]),
            E('h3', {}, 'Responder status'),
            diagnostics,
            E('p', {}, [E('a', { href: L.url('admin', 'status', 'logs') }, 'View system log'),
                ' for more information for debugging. ERR seems not an error, but the vendor-specific code.'])
        ]);
    },
    handleSaveApply: null,
    handleSave: null,
    handleReset: null
});
