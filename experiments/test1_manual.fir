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
    module o0:
        ; ignore ignore iostate, mem and loopstate for now?
        ; also ignore ready valid (valid: UInt<1>, flip ready: UInt<1>)
        ; purely combinatorial
        ;input argument: {a1: SInt<32>, a2: SInt<32>, a3: UInt<1>, a4: UInt<1>, a5: UInt<1>}
        ;output result: {o8: SInt<32>}
        input a1: SInt<32>
        input a2: SInt<32>
        input a3: UInt<1>
        input a4: UInt<1>
        input a5: UInt<1>
        output o8: SInt<32>
        node o6 = gt(a1, a2)
        node o7 = o6
        when eq(o7,UInt<1>(0)):
            node a9 = a1
            node a10 = a2
            node o11 = eq(a9, a10)
            node o12 = o11
            wire o13: SInt<32>
            when eq(o12, UInt<1>(0)):
                node a14 = a10
                node a15 = a9
                node o16 = SInt<32>(0)
                o13 <= o16
            else:
                node a17 = a10
                node a18 = a9
                node o19 = add(a18, a17)
                o13 <= o19
            o8 <= o13
        else:
            node a20 = a1
            node a21 = a2
            o8 <= a20
        o8 <= o8
