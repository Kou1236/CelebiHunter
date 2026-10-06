"""Pure terminal arithmetic compared with saved native fixture and old oracle."""
from pathlib import Path
import ctypes as C
import hashlib
import json
import re
import time
import sys
sys.dont_write_bytecode = True

ROOT = Path(__file__).resolve().parent
RNG = ROOT.parents[1] / 'rng'
CACHED = RNG / 'data'
ORACLE = ROOT / 'fixtures/scalar_oracle.c'
NATIVE = ROOT / 'fixtures/one_encounter.json'
FIXTURE = ROOT / 'fixtures/controlled_terminal_comparison.json'

class Prepared(C.Structure):
    _fields_ = [('sums',(C.c_uint32*256)*2),('ready',C.c_uint32)]
class Detail(C.Structure):
    _fields_ = [(n,C.c_uint32) for n in ('dv','branch')]+[
        ('prefix_sums',C.c_uint32*2),('before_a',C.c_uint32),('before_s',C.c_uint32),
        ('ordinary_count',C.c_uint32),('ordinary_div',(C.c_uint32*2)*4),
        ('ordinary_sub',C.c_uint32*4)]

def sha(p): return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def main():
    begun = time.perf_counter()
    header = CACHED/'profile_data.h'
    fixture = json.loads(FIXTURE.read_bytes())
    native = json.loads(NATIVE.read_bytes())
    assert sha(header) == '92f538dfe9809e667e4c32cc6008705f5f95c61d02890336e896bc0c6cb49c21'
    assert sha(NATIVE) == 'ae820ec228c5dd7296c52b938c66f324b29ec58dff36bb0c63e96d7cbc8960c7'
    block = re.search(r'sol61_prefix_hist\[2\]\[256\]\s*=\s*\{(.*?)\n\};',header.read_text(),re.S).group(1)
    values = [int(x) for x in re.findall(r'\b\d+\b',block)]
    assert len(values) == 512
    hist = [values[:256],values[256:]]
    assert [sum(h) for h in hist] == [611,611]
    raw = re.search(r'scalar_offsets\[2\]\[3\]\[4\]\[2\]\s*=\s*\{(.*?)\n\};',ORACLE.read_text(),re.S).group(1)
    branches = re.findall(r'\{((?:\{\d+,\d+\},?){3,4})\}',raw)
    assert len(branches) == 6
    offsets = [[tuple(map(int,p)) for p in re.findall(r'\{(\d+),(\d+)\}',branch)] for branch in branches]
    # Scalar expansion of all 611 operands is independent of cached translation.
    sums = [[sum((x+k)&255 for k,n in enumerate(h) for _ in range(n))
             for x in range(256)] for h in hist]
    dll = C.CDLL(str(Path(sys.argv[1]).resolve()))
    dll.cq_terminal_prepare.argtypes = [C.POINTER(Prepared)]
    dll.cq_terminal_prepare.restype = C.c_int
    dll.terminal_eval.argtypes = [C.c_uint32]*4
    dll.terminal_eval.restype = C.c_uint32
    dll.cq_terminal_eval_prepared.argtypes = [C.POINTER(Prepared)]+[C.c_uint32]*4
    dll.cq_terminal_eval_prepared.restype = C.c_uint32
    dll.cq_terminal_eval_detail.argtypes = [C.POINTER(Prepared)]+[C.c_uint32]*4+[C.POINTER(Detail)]
    dll.cq_terminal_eval_detail.restype = C.c_int
    dll.cq_terminal_is_shiny.argtypes = [C.c_uint32]
    dll.cq_terminal_is_shiny.restype = C.c_int
    prepared = Prepared()
    assert dll.cq_terminal_prepare(C.byref(prepared)) == 1 and prepared.ready == 611
    assert [list(side) for side in prepared.sums] == sums

    terminal = native['terminal_normalization']
    a,s = terminal['RNG_retained']; x = terminal['DIV_retained']; bg = terminal['BGThird_retained']
    assert (a,s,x,bg) == (106,89,108,0)
    detail = Detail()
    assert dll.cq_terminal_eval_detail(C.byref(prepared),a,s,x,bg,C.byref(detail))
    observed = native['normal_pairs_observed'][64:]
    assert len(observed) == 611
    native_sums = [sum(row[k] for row in observed) for k in ('DIV1','DIV2')]
    assert list(detail.prefix_sums) == native_sums == [77174,77287]
    assert [detail.before_a,detail.before_s] == native['ordinary_direct_pairs_observed'][0]['RNG_before'] == [224,69]
    native_pairs = [[row['DIV1'],row['DIV2']] for row in native['ordinary_direct_pairs_observed']]
    actual_pairs = [list(row) for row in detail.ordinary_div[:detail.ordinary_count]]
    assert actual_pairs == native_pairs == [[106,107],[109,109],[114,114],[116,116]]
    assert detail.branch == fixture['selected_branch'] == 1
    assert f'{detail.dv:04X}' == fixture['actual_DVs'] == native['actual_DVs'] == 'F985'
    fixture_result = dict(input=dict(a=a,s=s,x=x,bg=bg),packed_dv=detail.dv,
        DVs=f'{detail.dv:04X}',branch=detail.branch,prefix_sums=list(detail.prefix_sums),
        RNG_before_ordinary=[detail.before_a,detail.before_s],ordinary_pairs=actual_pairs,
        ordinary_sub=list(detail.ordinary_sub[:detail.ordinary_count]),
        exact_native_fixture_match=True)

    exact = 0; branch_coverage = [0,0,0]; shiny = 0
    for a,s in [(0,0),(255,255),(0,255),(255,0),(106,89),(42,73),(128,127),(1,254)]:
        for x in range(256):
            for bg in range(3):
                axis = 0 if bg == 2 else 1
                selected = []
                # Old independent scalar oracle enumerates item paths, then
                # admits only the branch selected by its actual Random bytes.
                for branch in range(3):
                    wide = a+sums[0][x]
                    aa = wide&255; ss = (s-sums[1][x]-(wide>>8))&255
                    outputs = []
                    for first,second in offsets[axis*3+branch]:
                        wide = aa+((x+first)&255); aa = wide&255
                        ss = (ss-((x+second)&255)-(wide>>8))&255
                        outputs.append(ss)
                    chosen = 0 if outputs[0] < 192 else 2 if outputs[1] < 20 else 1
                    if chosen == branch: selected.append((branch,(outputs[-2]<<8)|outputs[-1]))
                assert len(selected) == 1
                branch,dv = selected[0]
                assert dll.terminal_eval(a,s,x,bg) == dll.cq_terminal_eval_prepared(C.byref(prepared),a,s,x,bg) == dv
                assert dll.cq_terminal_eval_detail(C.byref(prepared),a,s,x,bg,C.byref(detail)) == 1
                assert detail.dv == dv and detail.branch == branch
                assert bool(dll.cq_terminal_is_shiny(dv)) == (dv&0xfff == 0xaaa and bool(dv&0x2000))
                branch_coverage[branch] += 1; exact += 1
                shiny += bool(dll.cq_terminal_is_shiny(dv))
    assert all(branch_coverage)
    # A fixed DIV input can admit shiny RNG inputs. Sweep only S for this
    # fixed test x; this tests the evaluator, never chooses a query's DIV.
    fixed_div_shiny = []
    for s in range(256):
        dv = dll.cq_terminal_eval_prepared(C.byref(prepared),0,s,8,0)
        assert dll.terminal_eval(0,s,8,0) == dv
        if dll.cq_terminal_is_shiny(dv):
            fixed_div_shiny.append(dict(a=0,s=s,x=8,bg=0,DVs=f'{dv:04X}'))
    assert fixed_div_shiny
    for atk in range(16):
        assert bool(dll.cq_terminal_is_shiny((atk<<12)|0xaaa)) == (atk in [2,3,6,7,10,11,14,15])
    invalid = [(256,0,0,0),(0,256,0,0),(0,0,256,0),(0,0,0,3)]
    for args in invalid:
        assert dll.terminal_eval(*args) == dll.cq_terminal_eval_prepared(C.byref(prepared),*args) == 0xffffffff
    blank = Prepared()
    assert dll.cq_terminal_eval_prepared(C.byref(blank),0,0,0,0) == 0xffffffff
    assert dll.cq_terminal_eval_prepared(None,0,0,0,0) == 0xffffffff
    assert not dll.cq_terminal_prepare(None)
    assert not dll.cq_terminal_is_shiny(0xffffffff)
    report = dict(status='passed_pure_controlled_terminal_eval_and_saved_native_F985_fixture',
        saved_native_fixture=fixture_result,scalar_DV_cases=exact,branch_coverage=branch_coverage,
        shiny_cases=shiny,prefix_sum_comparisons=512,prepared_bytes=C.sizeof(Prepared),
        fixed_actual_DIV_shiny_test_inputs=fixed_div_shiny,
        all_BG_phases=[0,1,2],input_DIV_domain_for_verification=256,
        candidate_DIV_selection=False,candidate_native_encounters=False,
        original_611_histogram_and_ordinary_offsets_retained=True,
        no_DIV_or_RNG_or_input_writes=True,no_device=True,new_encounters=0,
        future_DIV_forecast_qualified=False,hardware_prediction_verified=False,
        seconds=time.perf_counter()-begun)
    print(json.dumps({k:report[k] for k in ('status','scalar_DV_cases','branch_coverage','shiny_cases','seconds')}))

if __name__ == '__main__': main()
