"""Bounded pure waiting query checks; uses saved native waiting observations."""
from pathlib import Path
import ctypes as C
import hashlib
import json
import re
import time
import sys
sys.dont_write_bytecode = True
from check_terminal import Prepared

ROOT = Path(__file__).resolve().parent
RNG = ROOT.parents[1] / 'rng'
CACHED = RNG / 'data'
ORACLE = ROOT / 'fixtures/scalar_oracle.c'
CLOCK = RNG / 'candidate-clock/released_div_prediction.h'
NATIVE = ROOT / 'fixtures/released_boundary_validation.json'

class Clock(C.Structure):
    _fields_ = [(n,C.c_uint32) for n in ('div','div_countdown','timer_phase','budget','lcd_countdown')]
class Waiting(C.Structure):
    _fields_ = [('a',C.c_uint32),('s',C.c_uint32),('bg',C.c_uint32),('clock',Clock)]
class Result(C.Structure):
    _fields_ = [(n,C.c_uint32) for n in ('status','found','effective_n','raw_press_n',
        'div_x','branch','predicted_dv','expected_wait_add','expected_wait_sub','expected_bg',
        'min_n','max_n','checked_candidates','projected_units')]+[('target_clock',Clock)]

def sha(p): return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def clock_tuple(p): return tuple(getattr(p,n) for n,_ in Clock._fields_)
def boundary(seed,n):
    div,cd,phase,_,lcd = seed
    cost = 17556*n
    ticks = (cost+64-cd)//64
    cd = (cd-1-cost)%64+1; phase = (phase-cost)%256
    return ((div+ticks)%256,cd,phase,min(cd,phase+1),lcd)

def pair_at(seed):
    # Independent Python literal instruction charging, including reads before
    # their own writeback. Its outputs also compare with 256 saved native pairs.
    costs = (4,4,4,4,4,3,2,1,2,3,2,2,2,2,1,6,1,3,3,3,1,3,1,3,3)
    div,cd,phase,budget,lcd = seed
    pending = 0; reads = []
    for k,cost in enumerate(costs):
        if k in (19,24):
            reads.append(div)
            if len(reads) == 2: return reads
        pending += cost
        if pending >= budget:
            assert pending < lcd and pending < cd+64
            if pending >= cd: div = (div+1)%256; cd = cd+64-pending
            else: cd -= pending
            phase = (phase-pending)%256; lcd -= pending
            budget = min(cd,phase+1,lcd); pending = 0
    raise AssertionError('no Normal pair')

def main():
    begun = time.perf_counter()
    native = json.loads(NATIVE.read_bytes())
    assert native['status'] == 'passed256_native_integer_boundary_and_Normal_pair_checks'
    header = (CACHED/'profile_data.h').read_text()
    block = re.search(r'sol61_prefix_hist\[2\]\[256\]\s*=\s*\{(.*?)\n\};',header,re.S).group(1)
    h = [int(x) for x in re.findall(r'\b\d+\b',block)]
    sums = [[sum(count*((x+k)%256) for k,count in enumerate(h[side*256:(side+1)*256]))
        for x in range(256)] for side in range(2)]
    raw = re.search(r'scalar_offsets\[2\]\[3\]\[4\]\[2\]\s*=\s*\{(.*?)\n\};',ORACLE.read_text(),re.S).group(1)
    branches = re.findall(r'\{((?:\{\d+,\d+\},?){3,4})\}',raw)
    offsets = [[tuple(map(int,p)) for p in re.findall(r'\{(\d+),(\d+)\}',branch)] for branch in branches]
    def terminal(a,s,x,bg):
        found = []
        axis = 0 if bg == 2 else 1
        for branch in range(3):
            wide = a+sums[0][x]; aa = wide%256; ss = (s-sums[1][x]-wide//256)%256
            outputs = []
            for d1,d2 in offsets[axis*3+branch]:
                wide = aa+(x+d1)%256; aa = wide%256
                ss = (ss-(x+d2)%256-wide//256)%256; outputs.append(ss)
            chosen = 0 if outputs[0]<192 else 2 if outputs[1]<20 else 1
            if chosen == branch: found.append((branch,(outputs[-2]<<8)|outputs[-1]))
        assert len(found) == 1
        return found[0]
    dll = C.CDLL(str(Path(sys.argv[1]).resolve()))
    dll.cq_waiting_next.argtypes = [C.POINTER(Waiting)]; dll.cq_waiting_next.restype = C.c_int
    dll.cq_terminal_prepare.argtypes = [C.POINTER(Prepared)]; dll.cq_terminal_prepare.restype = C.c_int
    dll.cq_find.argtypes = [C.POINTER(Waiting),C.c_uint32,C.c_uint32,C.POINTER(Result)]; dll.cq_find.restype = C.c_int
    dll.cq_find_prepared.argtypes = [C.POINTER(Waiting),C.c_uint32,C.c_uint32,
        C.POINTER(Prepared),C.POINTER(Result)]; dll.cq_find_prepared.restype = C.c_int
    prepared = Prepared(); assert dll.cq_terminal_prepare(C.byref(prepared))
    origin = native['origin_clock']
    clock = (origin['DIV'],origin['DIV_countdown'],origin['timer_phase'],origin['budget'],113)
    state = Waiting(6,234,2,Clock(*clock)); a,s,bg = state.a,state.s,state.bg
    for n,row in enumerate(native['actual_rows']):
        assert clock_tuple(state.clock) == boundary(clock,n)
        assert pair_at(clock_tuple(state.clock)) == row['Normal_pair']
        wide = a+row['Normal_pair'][0]; a = wide%256
        s = (s-row['Normal_pair'][1]-wide//256)%256; bg = (bg+1)%3
        assert dll.cq_waiting_next(C.byref(state))
        assert (state.a,state.s,state.bg) == (a,s,bg)
        assert clock_tuple(state.clock) == (row['DIV'],row['DIV_countdown'],row['timer_phase'],row['budget'],113)

    cases = []; source_unchanged = True
    test_cases = [(6,234,2,clock,0,120),(6,234,2,clock,1,120),
        (255,0,0,clock,0,1),(106,89,0,boundary(clock,64),1,120),
        (6,234,2,clock,0,65535)]
    for aa,ss,bg,seed_clock,elapsed,lead in test_cases:
        source = Waiting(aa,ss,bg,Clock(*seed_clock)); before = bytes(source)
        result = Result(); status = dll.cq_find_prepared(C.byref(source),elapsed,lead,C.byref(prepared),C.byref(result))
        assert bytes(source) == before
        assert result.min_n == max(2,elapsed+lead+1) and result.max_n == elapsed+65536
        a,s,b = aa,ss,bg; expected = None; eligible = 0
        for n in range(elapsed+65536+1):
            c = boundary(seed_clock,n)
            if n >= max(2,elapsed+lead+1):
                eligible += 1; branch,dv = terminal(a,s,c[0],b)
                if dv&0xfff == 0xaaa and dv&0x2000:
                    expected = (n,a,s,b,c,branch,dv); break
            if n == elapsed+65536: break
            pair = pair_at(c); wide = a+pair[0]; a = wide%256
            s = (s-pair[1]-wide//256)%256; b = (b+1)%3
        assert result.checked_candidates == eligible
        if expected is None:
            assert status == result.status == 3 and not result.found
            assert result.projected_units == elapsed+65536
        else:
            n,a,s,b,c,branch,dv = expected
            assert status == result.status == 0 and result.found == 1
            assert (result.effective_n,result.raw_press_n,result.div_x,result.branch,result.predicted_dv,
                result.expected_wait_add,result.expected_wait_sub,result.expected_bg) == (n,n-1,c[0],branch,dv,a,s,b)
            assert clock_tuple(result.target_clock) == c and result.projected_units == n
        check = Result(); assert dll.cq_find(C.byref(source),elapsed,lead,C.byref(check)) == status
        assert bytes(check) == bytes(result)
        cases.append(dict(seed=dict(a=aa,s=ss,bg=bg,clock=seed_clock),elapsed=elapsed,lead=lead,
            status=status,effective_n=result.effective_n,raw_press_n=result.raw_press_n,
            terminal_DIV=result.div_x,terminal_RNG=[result.expected_wait_add,result.expected_wait_sub],
            terminal_bg=result.expected_bg,branch=result.branch,predicted_dv=f'{result.predicted_dv:04X}',
            eligible_candidates=eligible,projected_units=result.projected_units))
    assert any(c['status']==0 for c in cases) and any(c['status']==3 for c in cases)
    good = Waiting(6,234,2,Clock(*clock))
    for elapsed,lead,expected in [(2,120,1),(0,0,1),(0,65536,4),(1,0xffffffff,4)]:
        result = Result(); assert dll.cq_find(C.byref(good),elapsed,lead,C.byref(result)) == expected
        assert result.status == expected and not result.found
    bad = Waiting(6,234,2,Clock(216,39,38,39,112)); before = bytes(bad)
    assert not dll.cq_waiting_next(C.byref(bad)) and bytes(bad) == before
    result = Result(); assert dll.cq_find(C.byref(bad),0,120,C.byref(result)) == 2
    assert dll.cq_find(None,0,120,C.byref(result)) == 1
    assert dll.cq_find(C.byref(good),0,120,None) == 1
    report = dict(status='passed_bounded_earliest_actual_DIV_waiting_query',
        saved_native_released_units_checked=256,saved_native_Normal_pairs_checked=256,
        query_cases=cases,horizon_from_current=65536,max_seed_lag_units=1,
        earliest_hit_independent_scalar_checked=True,source_unchanged=source_unchanged,
        prepared_and_direct_results_identical=True,full_horizon_miss_checked=True,
        no_free_DIV_selection=True,no_candidate_encounter_replay=True,new_native_encounters=0,
        no_device=True,hardware_prediction_verified=False,
        seconds=time.perf_counter()-begun)
    print(json.dumps(dict(status=report['status'],cases=cases,seconds=report['seconds'])))

if __name__ == '__main__': main()
