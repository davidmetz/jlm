; unsigned int test(unsigned int a, unsigned int b){
;     while(a!=b){
;         if(a>b)
;             a -= b;
;         else
;             b -= a;
;     }
;     return a;
; }

; []{
;   o0 := LAMBDA
;     [a1, a2, a3, a4, a5]{
;       o6 := undef
;       o7 o8 o9 o10 o11 o12 := THETA a2 o6 a1 a3 a4 a5
;         [a13 <= a2, a14 <= o6, a15 <= a1, a16 <= a3, a17 <= a4, a18 <= a5]{
;           o19 := BITNE32 a15 a13
;           o20 := MATCH[1 -> 1, 0] o19
;           o21 o22 o23 := GAMMA o20 a13 a13 a15 a15
;             [a24 <= a13, a25 <= a13, a26 <= a15, a27 <= a15]{
;               o28 := CTL(0)
;             }[o21 <= o28, o22 <= a27, o23 <= a25]
;             [a29 <= a13, a30 <= a13, a31 <= a15, a32 <= a15]{
;               o33 := BITUGT32 a31 a29
;               o34 := CTL(1)
;               o35 := MATCH[1 -> 1, 0] o33
;               o36 o37 := GAMMA o35 a31 a29
;                 [a38 <= a31, a39 <= a29]{
;                   o40 := BITSUB32 a39 a38
;                 }[o36 <= a38, o37 <= o40]
;                 [a41 <= a31, a42 <= a29]{
;                   o43 := BITSUB32 a41 a42
;                 }[o36 <= o43, o37 <= a42]
;             }[o21 <= o34, o22 <= o36, o23 <= o37]
;         }[o21, o7 <= o23, o8 <= a15, o9 <= o22, o10 <= a16, o11 <= a17, o12 <= a18]
;     }[o8, o10, o11, o12]
; }[o0]

circuit lambda0:
    module lambda0:
        input clk: Clock
        input reset: UInt<1>
        input i0: {flip ready: UInt<1>, valid: UInt<1>, data: UInt<32>}
        input i1: {flip ready: UInt<1>, valid: UInt<1>, data: UInt<32>}
        input i2: {flip ready: UInt<1>, valid: UInt<1>, data: UInt<32>}
        input i3: {flip ready: UInt<1>, valid: UInt<1>, data: UInt<32>}
        input i4: {flip ready: UInt<1>, valid: UInt<1>, data: UInt<32>}
        output o0: {flip ready: UInt<1>, valid: UInt<1>, data: UInt<1>}
        output o1: {flip ready: UInt<1>, valid: UInt<1>, data: UInt<1>}
        output o2: {flip ready: UInt<1>, valid: UInt<1>, data: UInt<1>}
        output o3: {flip ready: UInt<1>, valid: UInt<1>, data: UInt<1>}
