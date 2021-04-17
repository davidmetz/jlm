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

    module neq0:
        ; io
        input clk: Clock
        input reset: UInt<1>
        input i0: {flip ready: UInt<1>, valid: UInt<1>, data: UInt<32>}
        input i1: {flip ready: UInt<1>, valid: UInt<1>, data: UInt<32>}
        output o0: {flip ready: UInt<1>, valid: UInt<1>, data: UInt<1>}
        ; registers
        reg i0_valid_reg: UInt<1>, clk with: (reset => (reset, UInt<1>(0)))
        reg i0_data_reg: UInt<32>, clk ; no need for reset for data?
        reg i1_valid_reg: UInt<1>, clk with: (reset => (reset, UInt<1>(0)))
        reg i1_data_reg: UInt<32>, clk ; no need for reset for data?
        ; logic
        o0.data <= neq(i0_data_reg, i1_data_reg)
        ; handshaking
        i0.ready <= not(i0_valid_reg)
        i1.ready <= not(i1_valid_reg)
        node inputs_valid = and(i0_valid_reg, i1_valid_reg)
        o0.valid <= and(inputs_valid, o0.ready)
        when o0.valid:
            i0_valid_reg <= UInt<1>(0)
            i1_valid_reg <= UInt<1>(0)
        when i0.valid:
            i0_valid_reg <= UInt<1>(1)
            i0_data_reg <= i0.data
        when i1.valid:
            i1_valid_reg <= UInt<1>(1)
            i1_data_reg <= i1.data

    module theta0:
        ; io
        input clk: Clock
        input reset: UInt<1>
        input i0: {flip ready: UInt<1>, valid: UInt<1>, data: UInt<32>}
        input i1: {flip ready: UInt<1>, valid: UInt<1>, data: UInt<32>}
        input i2: {flip ready: UInt<1>, valid: UInt<1>, data: UInt<32>}
        input i3: {flip ready: UInt<1>, valid: UInt<1>, data: UInt<1>}
        input i4: {flip ready: UInt<1>, valid: UInt<1>, data: UInt<1>}
        input i5: {flip ready: UInt<1>, valid: UInt<1>, data: UInt<1>}
        output o0: {flip ready: UInt<1>, valid: UInt<1>, data: UInt<32>}
        output o1: {flip ready: UInt<1>, valid: UInt<1>, data: UInt<32>}
        output o2: {flip ready: UInt<1>, valid: UInt<1>, data: UInt<32>}
        output o3: {flip ready: UInt<1>, valid: UInt<1>, data: UInt<1>}
        output o4: {flip ready: UInt<1>, valid: UInt<1>, data: UInt<1>}
        output o5: {flip ready: UInt<1>, valid: UInt<1>, data: UInt<1>}
        ; instances
        inst neq0_0 of neq0
        neq0_0.clk <= clk
        neq0_0.reset <= reset
        neq0_0.i0.data <= i0.data
        neq0_0.i0.valid <= i0.valid
        neq0_0.i1.data <= i1.data
        neq0_0.i1.valid <= i1.valid
        inst gamma0_0 of gamma0
        gamma0_0.clk <= clk
        gamma0_0.reset <= reset
        gamma0_0.i0.data <= neq0_0.o0.data
        gamma0_0.i0.valid <= neq0_0.o0.valid
        gamma0_0.i1.data <= i0.data
        gamma0_0.i1.valid <= i0.valid
        gamma0_0.i2.data <= i1.data
        gamma0_0.i2.valid <= i1.valid
        gamma0_0.i3.data <= i2.data
        gamma0_0.i3.valid <= i2.valid
        gamma0_0.i4.data <= i3.data
        gamma0_0.i4.valid <= i3.valid
        gamma0_0.i5.data <= i4.data
        gamma0_0.i5.valid <= i4.valid
        gamma0_0.i6.data <= i5.data
        gamma0_0.i6.valid <= i5.valid
        ; handshaking
        i0.ready <= and(neq0_0.i0.ready, gamma0_0.i1.ready)
        i1.ready <= and(neq0_0.i1.ready, gamma0_0.i2.ready)
        i2.ready <= UInt<1>(1)
        i3.ready <= and(gamma0_0.i3.ready, )




