"""Local boot tracing. Run from LLDB after module relocation."""
import lldb
import json
from pathlib import Path
from workspace import ROOT, load_game

counts = {}
labels = {}

def read_string(process, address):
    error = lldb.SBError()
    count = process.ReadUnsignedFromMemory(address + 16, 4, error)
    if error.Fail() or count > 8192:
        return '<invalid string>'
    return process.ReadMemory(address + 20, count * 2, error).decode('utf-16-le', 'replace')

def trace(frame, location, _):
    ident = location.GetBreakpoint().GetID()
    counts[ident] = counts.get(ident, 0) + 1
    label = labels[ident]
    if counts[ident] > (300 if label in ('UnityLog', 'UnityException') else 12):
        return False
    regs = [frame.FindRegister('x%d' % i).GetValueAsUnsigned() for i in range(5)]
    print('SS_TRACE', label, ' '.join('x%d=%x' % (i, x) for i, x in enumerate(regs)), flush=True)
    if label == 'UnityLog':
        print('SS_LOG', read_string(frame.GetThread().GetProcess(), regs[2]), flush=True)
    if label == 'AddressablesInit':
        print('SS_PATH', read_string(frame.GetThread().GetProcess(), regs[1]), flush=True)
    if label in ('stat', 'open', 'GetEntryType'):
        process = frame.GetThread().GetProcess()
        error = lldb.SBError()
        print('SS_FILE', process.ReadCStringFromMemory(regs[1] if label == 'GetEntryType' else regs[0], 1024, error), flush=True)
    if label == 'UnityException':
        process = frame.GetThread().GetProcess()
        error = lldb.SBError()
        message = process.ReadPointerFromMemory(regs[0] + 0x18, error)
        print('SS_EXCEPTION', read_string(process, message), flush=True)
        for f in frame.GetThread():
            print('SS_STACK', hex(f.GetPC()), flush=True)
    return False

def install(debugger, game_id='silksong', root=ROOT):
    game = load_game(game_id, root)
    game.require_adapted()
    profile = json.loads((game.game_dir / 'debug/lldb.json').read_text())
    if profile.get('schema_version') != 1: raise ValueError('Unsupported LLDB profile schema.')
    target = debugger.GetSelectedTarget()
    counts.clear()
    labels.clear()
    for item in profile.get('module_breakpoints', []):
        index = game.module_names.index(item['module'])
        value = target.EvaluateExpression('(unsigned long)g_mod[%d].base' % index)
        if value.GetError().Fail(): raise RuntimeError('Cannot read the module base: ' + item['module'])
        base = value.GetValueAsUnsigned()
        offset, label = int(item['offset'], 0), item['label']
        point = target.BreakpointCreateByAddress(base + offset)
        labels[point.GetID()] = label
        point.SetScriptCallbackFunction('ss_lldb.trace')
        print('GUEST_TRACE_POINT', game.id, item['module'], hex(base + offset), label, flush=True)
    for item in profile.get('symbol_breakpoints', []):
        symbol, label = item['symbol'], item['label']
        if item['kind'] == 'host':
            point = target.BreakpointCreateByName(symbol)
        elif item['kind'] == 'guest':
            address = target.EvaluateExpression('(unsigned long)guest_sym(%s)' % json.dumps(symbol)).GetValueAsUnsigned()
            if not address: raise RuntimeError('Guest symbol does not exist: ' + symbol)
            point = target.BreakpointCreateByAddress(address)
        else: raise ValueError('Unknown LLDB breakpoint kind: ' + str(item['kind']))
        labels[point.GetID()] = label
        point.SetScriptCallbackFunction('ss_lldb.trace')
