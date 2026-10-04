"""Compile clean-US ROM instruction sites to C with the live host CPU ABI.

Generated output stays local, like src/gen. Static reachability only selects
candidate PCs; each C edge checks the live opcode and retains operand/bus,
M/X, stack, decimal and cycle semantics. No basic block skips a host hook.
The framework's pinned MIT interpreter is the semantic source, not a runtime
decoder. Its instruction switch becomes constant C actions at ROM PC labels.
"""
from pathlib import Path
import argparse, collections, hashlib, json, re, sys

ROOT=Path(__file__).resolve().parent.parent
sys.path.insert(0,str(ROOT/'snesrecomp/recompiler'))
from snes65816 import decode_insn

def generate(rom_path,output):
    rom=rom_path.read_bytes()
    fingerprint=2166136261
    for b in rom: fingerprint=((fingerprint^b)*16777619)&0xffffffff
    if len(rom)!=524288 or fingerprint!=0xec01686a:
        raise ValueError('Requires the clean 512 KiB US ROM (FNV ec01686a)')
    source=(ROOT/'snesrecomp/runner/src/snes/interp816.c').read_text()
    helpers=source[source.index('static uint8_t interp816_readOpcode(Interp816* cpu) {'):
                   source.index('static void interp816_doOpcode(Interp816* cpu, uint8_t opcode) {')]
    helpers='''static uint8_t interp816_read(Interp816 *cpu,uint32_t adr) {return cpu->read(cpu->mem,adr);}
static void interp816_write(Interp816 *cpu,uint32_t adr,uint8_t val) {cpu->write(cpu->mem,adr,val);}
static uint8_t interp816_getFlags(Interp816 *cpu);
static void interp816_setFlags(Interp816 *cpu,uint8_t val);
'''+helpers
    helpers=helpers.replace('uint8_t interp816_getFlags(', 'static uint8_t interp816_getFlags(').replace(
        'void interp816_setFlags(', 'static void interp816_setFlags(').replace('static static ','static ')
    helpers=helpers.replace('interp816_','sc_program_')
    cases=re.findall(r'    case 0x([0-9a-f]{2}): \{(.*?)\n    \}',source,re.S)
    assert len(cases)==256,len(cases)
    bodies={int(op,16):re.sub(r'\n      break;\s*$','',body).replace('interp816_','sc_program_') for op,body in cases}
    # BRK's historical bridge callback is outside this native program tier.
    bodies.pop(0)
    cycles=[int(v) for v in re.search(r'cyclesPerOpcode\[256\] = \{(.*?)\};',source,re.S).group(1).replace('\n','').split(',') if v.strip()]
    assert len(cycles)==256
    roots={int(bank,16)*65536+int(pc,16) for bank,pc in re.findall(r'void bank_([0-9a-f]{2})_([0-9a-f]{4})\(', (ROOT/'recomp/funcs.h').read_text())}
    for vector in (0xffe4,0xffea,0xffee,0xfffc): roots.add(int.from_bytes(rom[vector-0x8000:vector-0x8000+2],'little'))
    # The city tool and animated-CHR dispatchers call through ROM tables
    # rather than literal JSR targets. Include every command and animation
    # phase, not just entries observed in a profiling replay.
    # This only extends static coverage: emitted actions retain live opcode,
    # operand and CPU-width checks, and each host hook still runs per edge.
    indirect_tables=[]
    for dispatch,table,count in ((0x018e34,0x019cfa,56),(0x008741,0x008745,24),
        (0x00821e,0x008223,11),(0x018985,0x0188ef,12),
        (0x03d28f,0x03d255,23),(0x0593bd,0x0593c1,5),
        (0x01b60f,0x01830c,3),(0x01b62b,0x018312,3)):
        bank=dispatch>>16;offset=bank*32768+(dispatch&65535)-0x8000
        insn=decode_insn(rom,offset,dispatch&65535,bank,0,0)
        assert insn.opcode==0xfc and insn.operand==(table&65535)
        offset=(table>>16)*32768+(table&65535)-0x8000
        targets=[int.from_bytes(rom[offset+i*2:offset+i*2+2],'little') for i in range(count)]
        assert all(target>=0x8000 for target in targets)
        roots.update(bank*65536+target for target in targets)
        indirect_tables.append({'dispatch':f'{dispatch:06x}','table':f'{table:06x}',
                                'entries':count,'distinct_targets':len(set(targets))})
    # Verified callees consume inline operand bytes and adjust their stacked
    # return address. Reachability must resume at the real caller instruction,
    # rather than decoding the embedded operands as opcodes. Native C actions
    # still execute the original call, operand reads and stack manipulation.
    inline_arguments={0x0098a0:2,0x03a2f5:3,0x03a350:3,0x03a3cf:3,0x03a421:3}
    for target,skip in inline_arguments.items():
        at=(target>>16)*32768+(target&65535)-0x8000
        prefix=(bytes.fromhex('e2 20 c2 10 fa 68 48 8b 48 ab e8 c2 20 bd 00 00 e8 ab da')
                if skip==2 else bytes.fromhex('c2 30 68 a8 18 69 03 00 48'))
        assert rom[at:at+len(prefix)]==prefix,hex(target)
    pending=collections.deque((pc,m,x) for pc in sorted(roots) for m in (0,1) for x in (0,1))
    seen=set(); sites={}; branches={0x10,0x30,0x50,0x70,0x90,0xb0,0xd0,0xf0}
    def add(pc,m,x):
        if 0<=pc>>16<6 and (pc&65535)>=0x8000: pending.append((pc,m,x))
    def any_width(pc):
        for m in (0,1):
            for x in (0,1): add(pc,m,x)
    while pending:
        pc,m,x=pending.popleft()
        if (pc,m,x) in seen: continue
        seen.add((pc,m,x)); bank=pc>>16; local=pc&65535
        offset=bank*32768+local-0x8000
        if bank>=6 or local<0x8000 or offset+4>len(rom): continue
        insn=decode_insn(rom,offset,local,bank,m,x)
        if not insn or insn.opcode==0: continue
        op=insn.opcode;sites[pc]=op
        after=bank*65536+((local+insn.length)&65535)
        if op in (0x60,0x6b,0x40,0xdb): continue
        if op in (0x4c,0x5c):
            add(bank*65536+insn.operand if op==0x4c else insn.operand,m,x);continue
        if op in (0x6c,0x7c,0xdc): continue
        if op in (0x80,0x82): add(bank*65536+insn.operand,m,x);continue
        if op in branches: add(bank*65536+insn.operand,m,x)
        if op in (0x20,0x22,0xfc):
            target=bank*65536+insn.operand if op==0x20 else insn.operand
            if op!=0xfc:any_width(target)
            skip=inline_arguments.get(target,0) if op!=0xfc else 0
            any_width(bank*65536+((local+insn.length+skip)&65535));continue
        if op in (0x28,0x02,0xfb): any_width(after);continue
        if op in (0xc2,0xe2):
            if rom[offset+1]&32:m=int(op==0xe2)
            if rom[offset+1]&16:x=int(op==0xe2)
        add(after,m,x)
    # The host replaces this verified four-byte STA with four NOPs to keep
    # the view cursor intact. Compile both the clean-ROM instruction and each
    # patched edge; all other live opcode changes still decline. Operand-byte
    # entry points retain their original opcode semantics if ever admitted.
    assert rom[0x40fb:0x40ff]==bytes.fromhex('8f b5 21 7e')
    patch_sites=set(range(0xc0fb,0xc0ff))
    for pc in patch_sites:sites[pc]=rom[pc-0x8000]
    output.mkdir(parents=True,exist_ok=True)
    banner='''/* Generated by tools/compile_native_program.py; do not hand edit.
 * Instruction semantics adapted from the pinned LakeSnes/snesrecomp core.
 * Copyright (c) 2021-2023 angelo_wf and contributors, MIT.
 * See THIRD_PARTY_ATTRIBUTION.md. ROM content and generated code stay local.
 */
'''
    (output/'sc_program_helpers.h').write_text(banner+'#pragma once\n#include "snes/interp816.h"\n'+helpers)
    # Membership, rather than another instruction fetch, admits a connected
    # host lane. The C action still validates the live opcode on its real bus.
    masks=[]
    for bank in range(6):
        bits=bytearray(4096)
        for pc in sites:
            if pc>>16==bank:
                bit=(pc&65535)-0x8000;bits[bit>>3]|=1<<(bit&7)
        masks.append('{'+','.join(str(v) for v in bits)+'}')
    (output/'sc_program_sites.h').write_text(banner+'#pragma once\nstatic const unsigned char sc_program_sites[6][4096]={\n'+',\n'.join(masks)+'\n};\n')
    counts={}
    for bank in range(6):
        grouped=collections.defaultdict(list)
        for pc,op in sites.items():
            if pc>>16==bank: grouped[op].append(pc&65535)
        counts[f'{bank:02x}']=sum(map(len,grouped.values()))
        lines=[banner,'#include "sc_program.h"\n#include "sc_program_helpers.h"',
            'extern uint32_t g_interp816_cur_pc;',f'unsigned ScProgramBank{bank:02x}(Interp816 *cpu) {{',
            '    switch(cpu->pc) {']
        for op,addresses in sorted(grouped.items()):
            lines += [f'    case 0x{pc:04x}:' for pc in sorted(addresses)]
            lines += ['    {','      uint32_t address=((uint32_t)cpu->k<<16)|cpu->pc;',
                      '      uint8_t prior_cycles=cpu->cyclesUsed;',
                      '      cpu->cyclesUsed=0;cpu->pc++;',
                      '      uint8_t live=cpu->read(cpu->mem,address);']
            if bank==0 and any(pc in patch_sites for pc in addresses):
                lines += ['      if(live==0xea && address>=0xc0fb && address<=0xc0fe) {',
                          '        g_interp816_cur_pc=address;cpu->cyclesUsed=2;return 2;','      }']
            lines += [f'      if(live!=0x{op:02x}) {{cpu->pc--;cpu->cyclesUsed=prior_cycles;return 0;}}',
                      '      g_interp816_cur_pc=address;',
                      f'      cpu->cyclesUsed={cycles[op]};',bodies[op],
                      '      return cpu->cyclesUsed;','    }']
        lines += ['    default:return 0;','    }','}']
        (output/f'sc_program_bank{bank:02x}.c').write_text('\n'.join(lines)+'\n')
    # Direct C control flow in the UI/driver banks. Keep translation units
    # bounded, and dispatch by PC only on page entry or an unexpected target.
    # Every edge still prepares host state and retires on the real event clock.
    block_pages={}
    for bank in range(2):
        pages=collections.defaultdict(dict)
        for pc,op in sites.items():
            if pc>>16==bank:pages[(pc>>8)&255][pc&65535]=op
        block_pages[f'{bank:02x}']=len(pages)
        for chunk in range(8):
            selected={p:v for p,v in pages.items() if (p-128)//16==chunk}
            lines=[banner,'#include "sc_program_flow.h"\n#include "sc_program_helpers.h"',
                   'extern uint32_t g_interp816_cur_pc;']
            if chunk==0:
                for page in sorted(pages):
                    lines.append(f'void ScProgramPage{bank:02x}_{page:02x}(Interp816 *,ScProgramFlow *);')
                lines += [f'void ScProgramBlocks{bank:02x}(Interp816 *cpu,ScProgramFlow *flow) {{',
                    '    static void (*const pages[128])(Interp816 *,ScProgramFlow *)={']
                lines += [f'      ScProgramPage{bank:02x}_{p:02x},' if p in pages else '      0,' for p in range(128,256)]
                lines += ['    };','    if(cpu->pc<0x8000) return;',
                          '    if(pages[(cpu->pc>>8)-128]) pages[(cpu->pc>>8)-128](cpu,flow);','}']
            for page,entries in sorted(selected.items()):
                lines += [f'void ScProgramPage{bank:02x}_{page:02x}(Interp816 *cpu,ScProgramFlow *flow) {{',
                          '    switch(cpu->pc) {']
                lines += [f'    case 0x{pc:04x}:goto pc_{pc:04x};' for pc in sorted(entries)]
                lines += ['    default:return;','    }']
                for pc,op in sorted(entries.items()):
                    lines += [f'pc_{pc:04x}:', '    {',
                        '      if(!flow->before(flow->context,cpu)) {flow->keep_running=false;return;}',
                        f'      if(cpu->k!=0x{bank:02x} || cpu->pc!=0x{pc:04x}) {{ScProgramFlowChanged(cpu,flow);return;}}',
                        f'      uint32_t address=0x{bank:02x}{pc:04x};',
                        '      uint8_t prior_cycles=cpu->cyclesUsed;',
                        '      cpu->cyclesUsed=0;cpu->pc++;',
                        '      uint8_t live=cpu->read(cpu->mem,address);']
                    if bank==0 and pc in patch_sites:
                        lines += ['      if(live==0xea) {',
                            '        g_interp816_cur_pc=address;cpu->cyclesUsed=2;',
                            '        if(!ScProgramFlowRetire(flow,cpu,2,true)) return;',
                            f'        if(cpu->k==0 && cpu->pc==0x{pc+1:04x}) goto pc_{pc+1:04x};',
                            '        return;','      }']
                    lines += [f'      if(live!=0x{op:02x}) {{cpu->pc--;cpu->cyclesUsed=prior_cycles;',
                        '        unsigned cost=flow->fallback(flow->context,cpu);',
                        '        ScProgramFlowRetire(flow,cpu,cost?cost:1,false);return;}',
                        '      g_interp816_cur_pc=address;',f'      cpu->cyclesUsed={cycles[op]};',bodies[op],
                        '      if(!ScProgramFlowRetire(flow,cpu,cpu->cyclesUsed,true)) return;',
                        f'      if(cpu->k!=0x{bank:02x}) return;']
                    targets=set()
                    for m in (0,1):
                        for x in (0,1):
                            insn=decode_insn(rom,bank*32768+pc-0x8000,pc,bank,m,x)
                            if op in (0x60,0x6b,0x40,0xdb,0x02,0x6c,0x7c,0xdc,0xfc):continue
                            if op in (0x4c,0x20):targets.add(insn.operand)
                            elif op in (0x5c,0x22):
                                if insn.operand>>16==bank:targets.add(insn.operand&65535)
                            elif op in (0x80,0x82):targets.add(insn.operand)
                            else:
                                targets.add((pc+insn.length)&65535)
                                if op in branches:targets.add(insn.operand)
                                if op in (0x44,0x54):targets.add(pc)
                    lines += [f'      if(cpu->pc==0x{target:04x}) goto pc_{target:04x};'
                              for target in sorted(targets) if target in entries]
                    lines += ['      return;','    }']
                lines += ['}']
            (output/f'sc_program_bank{bank:02x}_blocks_{chunk}.c').write_text('\n'.join(lines)+'\n')
    manifest={'format':1,'rom_fnv':'ec01686a','semantic_source_sha256':hashlib.sha256(source.encode()).hexdigest(),
              'roots':len(roots),'width_states':len(seen),'sites':len(sites),'banks':counts,
              'direct_c_block_pages':block_pages,
              'indirect_dispatch_tables':indirect_tables,
              'inline_argument_bytes':{f'{target:06x}':skip for target,skip in inline_arguments.items()},
              'live_patch_variants':{'00:c0fb..c0fe':'verified view-cursor STA replaced with four native NOP edges'},
              'note':'Live widths/operands/bus accesses are retained; reachability controls coverage only. No opcode decoder executes inside emitted C edges.'}
    (output/'manifest.json').write_text(json.dumps(manifest,indent=2))
    print(json.dumps(manifest))

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--rom',type=Path,required=True)
    parser.add_argument('--out',type=Path,default=ROOT/'src/program_gen')
    args=parser.parse_args();generate(args.rom,args.out)
