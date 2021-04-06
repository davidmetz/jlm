; C code:
; int test(int a, int b){
;     int c = 0;
;     if(a>b){
;         return a;
;     } else if(a==b){
;         return a+b;
;     }
;     return c;
; }

; RVSDG graph:
; []{
;   o0 := LAMBDA
;     [a1, a2, a3, a4, a5]{
;       o6 := BITSGT32 a1 a2
;       o7 := MATCH[1 -> 1, 0] o6
;       o8 := GAMMA o7 a1 a2
;         [a9 <= a1, a10 <= a2]{
;           o11 := BITEQ32 a9 a10
;           o12 := MATCH[1 -> 1, 0] o11
;           o13 := GAMMA o12 a10 a9
;             [a14 <= a10, a15 <= a9]{
;               o16 := BITS32(0)
;             }[o13 <= o16]
;             [a17 <= a10, a18 <= a9]{
;               o19 := BITADD32 a18 a17
;             }[o13 <= o19]
;         }[o8 <= o13]
;         [a20 <= a1, a21 <= a2]{
;         }[o8 <= a20]
;     }[o8, a3, a4, a5]
; }[o0]

circuit o0:
    module gt_module:
        input clk: Clock
        input reset: UInt<1>
        input i: {flip ready: UInt<1>, valid: UInt<1>, i0: UInt<32>, i1: UInt<32>}
        output o: {flip ready: UInt<1>, valid: UInt<1>, o0: UInt<1>}
        reg valid_reg: UInt<1>, clk with: (reset => (reset, UInt<1>(0)))
        reg o0_reg: UInt<1>, clk ; no need for reset for data?
        node i0 = i.i0
        node i1 = i.i1
        node o0 = gt(asSInt(i0), asSInt(i1))
        o.valid <= valid_reg
        i.ready <= or(o.ready, not(valid_reg)) ; combinational path? loops? solve in loop logic?
        when o.ready:
            valid_reg <= UInt<1>(0)
        when and(i.ready, i.valid):
            valid_reg <= UInt<1>(1)
            o0_reg <= o0
        o.o0 <= o0_reg

    module eq_module:
        input clk: Clock
        input reset: UInt<1>
        input i: {flip ready: UInt<1>, valid: UInt<1>, i0: UInt<32>, i1: UInt<32>}
        output o: {flip ready: UInt<1>, valid: UInt<1>, o0: UInt<1>}
        reg valid_reg: UInt<1>, clk with: (reset => (reset, UInt<1>(0)))
        reg o0_reg: UInt<1>, clk ; no need for reset for data?
        node i0 = i.i0
        node i1 = i.i1
        node o0 = eq(i0, i1)
        o.valid <= valid_reg
        i.ready <= or(o.ready, not(valid_reg)) ; combinational path? loops? solve in loop logic?
        when o.ready:
            valid_reg <= UInt<1>(0)
        when and(i.ready, i.valid):
            valid_reg <= UInt<1>(1)
            o0_reg <= o0
        o.o0 <= o0_reg

    module add_module:
        input clk: Clock
        input reset: UInt<1>
        input i: {flip ready: UInt<1>, valid: UInt<1>, i0: UInt<32>, i1: UInt<32>}
        output o: {flip ready: UInt<1>, valid: UInt<1>, o0: UInt<32>}
        reg valid_reg: UInt<1>, clk with: (reset => (reset, UInt<1>(0)))
        reg o0_reg: UInt<32>, clk ; no need for reset for data?
        node i0 = i.i0
        node i1 = i.i1
        node o0 = asUInt(add(asSInt(i0), asSInt(i1)))
        o.valid <= valid_reg
        i.ready <= or(o.ready, not(valid_reg)) ; combinational path? loops? solve in loop logic?
        when o.ready:
            valid_reg <= UInt<1>(0)
        when and(i.ready, i.valid):
            valid_reg <= UInt<1>(1)
            o0_reg <= o0
        o.o0 <= o0_reg

    module gamma1_module:
        input clk: Clock
        input reset: UInt<1>
        input i: {flip ready: UInt<1>, valid: UInt<1>, i0: UInt<1>, i1: UInt<32>, i2: UInt<32>}
        output o: {flip ready: UInt<1>, valid: UInt<1>, o0: UInt<32>}
        reg valid_reg: UInt<1>, clk with: (reset => (reset, UInt<1>(0)))
        reg pred_reg: UInt<1>, clk
        reg o0_reg: UInt<32>, clk ; no need for reset for data?
        node i0 = i.i0
        node i1 = i.i1
        node o0 = asUInt(add(asSInt(i0), asSInt(i1)))
        o.valid <= valid_reg
        i.ready <= or(o.ready, not(valid_reg)) ; combinational path? loops? solve in loop logic?
        when o.ready:
            valid_reg <= UInt<1>(0)
        when and(i.ready, i.valid):
            valid_reg <= UInt<1>(1)
            o0_reg <= o0
        o.o0 <= o0_reg

    module o0:
        input clk: Clock
        input reset: UInt<1>
        input i: {flip ready: UInt<1>, valid: UInt<1>, i0: UInt<32>, i1: UInt<32>}
        output o: {flip ready: UInt<1>, valid: UInt<1>, o0: UInt<32>}
        reg valid_reg: UInt<1>, clk with: (reset => (reset, UInt<1>(0)))
        reg o0_reg: UInt<32>, clk ; no need for reset for data? one reg for each level?
        inst m0 of gt_module
        m0.i.valid <= i.valid
        m0.i.i0 <= i.i0
        m0.i.i1 <= i.i1
        inst m1 of gamma1_module
        m1.i.valid <= and(m0.o.valid, i.valid)
        m1.i.i0 <= m0.o.o0
        m1.i.i1 <= i.i0
        m1.i.i2 <= i.i1


        ; node o0 = add(i0, i1)
        o.valid <= valid_reg
        i.ready <= and(m0.i.ready, m1.i.ready)
        when o.ready:
            valid_reg <= UInt<1>(0)
        when and(i.ready, i.valid):
            valid_reg <= UInt<1>(1)
            o0_reg <= o0
        o.o0 <= o0_reg






;    module o0:
;        ; ignore ignore iostate, mem and loopstate for now?
;        ; also ignore ready valid (valid: UInt<1>, flip ready: UInt<1>)
;        ; purely combinatorial
;        ;input argument: {a1: SInt<32>, a2: SInt<32>, a3: UInt<1>, a4: UInt<1>, a5: UInt<1>}
;        ;output result: {o8: SInt<32>}
;        input a1: SInt<32>
;        input a2: SInt<32>
;        output o8: SInt<32>
;        node o6 = gt(a1, a2)
;        node o7 = o6
;        when eq(o7,UInt<1>(0)):
;            node a9 = a1
;            node a10 = a2
;            node o11 = eq(a9, a10)
;            node o12 = o11
;            wire o13: SInt<32>
;            when eq(o12, UInt<1>(0)):
;                node a14 = a10
;                node a15 = a9
;                node o16 = SInt<32>(0)
;                o13 <= o16
;            else:
;                node a17 = a10
;                node a18 = a9
;                node o19 = add(a18, a17)
;                o13 <= o19
;            o8 <= o13
;        else:
;            node a20 = a1
;            node a21 = a2
;            o8 <= a20
