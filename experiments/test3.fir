circuit test_lambda_mod:
    module subregion_mod0:
        ; io
        input clk: Clock
        input reset: UInt<1>
        input i0: {flip ready: UInt<1>, valid: UInt<1>, data: UInt<32>}
        input i1: {flip ready: UInt<1>, valid: UInt<1>, data: UInt<32>}
        input i2: {flip ready: UInt<1>, valid: UInt<1>, data: UInt<1>}
        input i3: {flip ready: UInt<1>, valid: UInt<1>, data: UInt<1>}
        input i4: {flip ready: UInt<1>, valid: UInt<1>, data: UInt<1>}
        output o0: {flip ready: UInt<1>, valid: UInt<1>, data: UInt<32>}
        output o1: {flip ready: UInt<1>, valid: UInt<1>, data: UInt<1>}
        output o2: {flip ready: UInt<1>, valid: UInt<1>, data: UInt<1>}
        output o3: {flip ready: UInt<1>, valid: UInt<1>, data: UInt<1>}
        ; instances
        inst BITUGT32_mod1_inst of BITUGT32_mod1
        BITUGT32_mod1_inst.clk <= clk
        BITUGT32_mod1_inst.reset <= reset
        BITUGT32_mod1_inst.i0.data <= i0.data
        BITUGT32_mod1_inst.i0.valid <= i0.valid
        BITUGT32_mod1_inst.i1.data <= i1.data
        BITUGT32_mod1_inst.i1.valid <= i1.valid
        inst MATCH_mod2_inst of MATCH_mod2
        MATCH_mod2_inst.clk <= clk
        MATCH_mod2_inst.reset <= reset
        MATCH_mod2_inst.i0.data <= BITUGT32_mod1_inst.o0.data
        MATCH_mod2_inst.i0.valid <= BITUGT32_mod1_inst.o0.valid
        inst gamma_mod3_inst of gamma_mod3
        gamma_mod3_inst.clk <= clk
        gamma_mod3_inst.reset <= reset
        gamma_mod3_inst.i0.data <= MATCH_mod2_inst.o0.data
        gamma_mod3_inst.i0.valid <= MATCH_mod2_inst.o0.valid
        gamma_mod3_inst.i1.data <= i0.data
        gamma_mod3_inst.i1.valid <= i0.valid
        gamma_mod3_inst.i2.data <= i1.data
        gamma_mod3_inst.i2.valid <= i1.valid
        o0.data <= gamma_mod3_inst.o0.data
        o0.valid <= gamma_mod3_inst.o0.valid
        o1.data <= i2.data
        o1.valid <= i2.valid
        o2.data <= i3.data
        o2.valid <= i3.valid
        o3.data <= i4.data
        o3.valid <= i4.valid
        i3.ready <= and(UInt<1>(1), o2.ready)
        i4.ready <= and(UInt<1>(1), o3.ready)
        i2.ready <= and(UInt<1>(1), o1.ready)
        gamma_mod3_inst.o0.ready <= and(UInt<1>(1), o0.ready)
        MATCH_mod2_inst.o0.ready <= and(UInt<1>(1), gamma_mod3_inst.i0.ready)
        BITUGT32_mod1_inst.o0.ready <= and(UInt<1>(1), MATCH_mod2_inst.i0.ready)
        i1.ready <= and(and(UInt<1>(1), BITUGT32_mod1_inst.i1.ready), gamma_mod3_inst.i2.ready)
        i0.ready <= and(and(UInt<1>(1), BITUGT32_mod1_inst.i0.ready), gamma_mod3_inst.i1.ready)
    module BITUGT32_mod1:
        ; io
        input clk: Clock
        input reset: UInt<1>
        input i0: {flip ready: UInt<1>, valid: UInt<1>, data: UInt<32>}
        input i1: {flip ready: UInt<1>, valid: UInt<1>, data: UInt<32>}
        output o0: {flip ready: UInt<1>, valid: UInt<1>, data: UInt<1>}
        ; registers
        reg i0_valid_reg: UInt<1>, clk with: (reset => (reset, UInt<1>(0)))
        reg i0_data_reg: UInt<32>, clk
        reg i1_valid_reg: UInt<1>, clk with: (reset => (reset, UInt<1>(0)))
        reg i1_data_reg: UInt<32>, clk
        ; logic
        o0.data <=gt(i0_data_reg, i1_data_reg) ; BITUGT32
        ; handshaking
        i0.ready <= not(i0_valid_reg)
        i1.ready <= not(i1_valid_reg)
        node inputs_valid = and(and(UInt<1>(1), i0_valid_reg), i1_valid_reg)
        o0.valid <= and(o0.ready, inputs_valid)
        when o0.valid:
            i0_valid_reg <= UInt<1>(0)
            i1_valid_reg <= UInt<1>(0)
        when i0.valid:
            i0_valid_reg <= UInt<1>(1)
            i0_data_reg <= i0.data
        when i1.valid:
            i1_valid_reg <= UInt<1>(1)
            i1_data_reg <= i1.data
    module MATCH_mod2:
        ; io
        input clk: Clock
        input reset: UInt<1>
        input i0: {flip ready: UInt<1>, valid: UInt<1>, data: UInt<1>}
        output o0: {flip ready: UInt<1>, valid: UInt<1>, data: UInt<1>}
        ; registers
        reg i0_valid_reg: UInt<1>, clk with: (reset => (reset, UInt<1>(0)))
        reg i0_data_reg: UInt<1>, clk
        ; logic
        o0.data <=i0_data_reg ; MATCH[1 -> 1, 0]
        ; handshaking
        i0.ready <= not(i0_valid_reg)
        node inputs_valid = and(UInt<1>(1), i0_valid_reg)
        o0.valid <= and(o0.ready, inputs_valid)
        when o0.valid:
            i0_valid_reg <= UInt<1>(0)
        when i0.valid:
            i0_valid_reg <= UInt<1>(1)
            i0_data_reg <= i0.data
    module gamma_mod3:
        ; io
        input clk: Clock
        input reset: UInt<1>
        input i0: {flip ready: UInt<1>, valid: UInt<1>, data: UInt<1>}
        input i1: {flip ready: UInt<1>, valid: UInt<1>, data: UInt<32>}
        input i2: {flip ready: UInt<1>, valid: UInt<1>, data: UInt<32>}
        output o0: {flip ready: UInt<1>, valid: UInt<1>, data: UInt<32>}
        ; registers
        reg pred_valid_reg: UInt<1>, clk with: (reset => (reset, UInt<1>(0)))
        reg pred_data_reg: UInt<1>, clk
        i0.ready <= not(pred_valid_reg)
        reg e0_valid_reg: UInt<1>, clk with: (reset => (reset, UInt<1>(0)))
        reg  e0_data_reg: UInt<32>, clk
        i1.ready <= not(e0_valid_reg)
        reg e1_valid_reg: UInt<1>, clk with: (reset => (reset, UInt<1>(0)))
        reg  e1_data_reg: UInt<32>, clk
        i2.ready <= not(e1_valid_reg)
        reg o0_processed_reg: UInt<1>, clk with: (reset => (reset, UInt<1>(0)))
        o0.valid <= UInt<1>(0) ; off by default
        o0.data is invalid ; off by default
        ; instances
        inst sr0 of subregion_mod4
        sr0.clk <= clk
        sr0.reset <= reset
        node pred_match_0 = and(eq(pred_data_reg, UInt<32>(0)), pred_valid_reg)
        sr0.i0.valid <= and(e0_valid_reg, pred_match_0)
        sr0.i0.data <= e0_data_reg
        sr0.i1.valid <= and(e1_valid_reg, pred_match_0)
        sr0.i1.data <= e1_data_reg
        sr0.o0.ready <= o0.ready
        when pred_match_0:
            o0.data <= sr0.o0.data
            o0.valid <= sr0.o0.valid
        inst sr1 of subregion_mod6
        sr1.clk <= clk
        sr1.reset <= reset
        node pred_match_1 = and(eq(pred_data_reg, UInt<32>(1)), pred_valid_reg)
        sr1.i0.valid <= and(e0_valid_reg, pred_match_1)
        sr1.i0.data <= e0_data_reg
        sr1.i1.valid <= and(e1_valid_reg, pred_match_1)
        sr1.i1.data <= e1_data_reg
        sr1.o0.ready <= o0.ready
        when pred_match_1:
            o0.data <= sr1.o0.data
            o0.valid <= sr1.o0.valid
        when o0.valid:
            o0_processed_reg <= UInt<1>(1)
        when i0.valid:
            pred_valid_reg <= UInt<1>(1)
            pred_data_reg <= i0.data
        when i1.valid:
            e0_valid_reg <= UInt<1>(1)
            e0_data_reg <= i1.data
        when i2.valid:
            e1_valid_reg <= UInt<1>(1)
            e1_data_reg <= i2.data
        node all_processed = and(UInt<1>(1), o0_processed_reg)
        when all_processed:
            pred_valid_reg <= UInt<1>(0)
            e0_valid_reg <= UInt<1>(0)
            e1_valid_reg <= UInt<1>(0)
            o0_processed_reg <= UInt<1>(0)
    module subregion_mod4:
        ; io
        input clk: Clock
        input reset: UInt<1>
        input i0: {flip ready: UInt<1>, valid: UInt<1>, data: UInt<32>}
        input i1: {flip ready: UInt<1>, valid: UInt<1>, data: UInt<32>}
        output o0: {flip ready: UInt<1>, valid: UInt<1>, data: UInt<32>}
        ; instances
        inst BITADD32_mod5_inst of BITADD32_mod5
        BITADD32_mod5_inst.clk <= clk
        BITADD32_mod5_inst.reset <= reset
        BITADD32_mod5_inst.i0.data <= i0.data
        BITADD32_mod5_inst.i0.valid <= i0.valid
        BITADD32_mod5_inst.i1.data <= i1.data
        BITADD32_mod5_inst.i1.valid <= i1.valid
        o0.data <= BITADD32_mod5_inst.o0.data
        o0.valid <= BITADD32_mod5_inst.o0.valid
        BITADD32_mod5_inst.o0.ready <= and(UInt<1>(1), o0.ready)
        i1.ready <= and(UInt<1>(1), BITADD32_mod5_inst.i1.ready)
        i0.ready <= and(UInt<1>(1), BITADD32_mod5_inst.i0.ready)
    module BITADD32_mod5:
        ; io
        input clk: Clock
        input reset: UInt<1>
        input i0: {flip ready: UInt<1>, valid: UInt<1>, data: UInt<32>}
        input i1: {flip ready: UInt<1>, valid: UInt<1>, data: UInt<32>}
        output o0: {flip ready: UInt<1>, valid: UInt<1>, data: UInt<32>}
        ; registers
        reg i0_valid_reg: UInt<1>, clk with: (reset => (reset, UInt<1>(0)))
        reg i0_data_reg: UInt<32>, clk
        reg i1_valid_reg: UInt<1>, clk with: (reset => (reset, UInt<1>(0)))
        reg i1_data_reg: UInt<32>, clk
        ; logic
        o0.data <=add(i0_data_reg, i1_data_reg) ; BITADD32
        ; handshaking
        i0.ready <= not(i0_valid_reg)
        i1.ready <= not(i1_valid_reg)
        node inputs_valid = and(and(UInt<1>(1), i0_valid_reg), i1_valid_reg)
        o0.valid <= and(o0.ready, inputs_valid)
        when o0.valid:
            i0_valid_reg <= UInt<1>(0)
            i1_valid_reg <= UInt<1>(0)
        when i0.valid:
            i0_valid_reg <= UInt<1>(1)
            i0_data_reg <= i0.data
        when i1.valid:
            i1_valid_reg <= UInt<1>(1)
            i1_data_reg <= i1.data
    module subregion_mod6:
        ; io
        input clk: Clock
        input reset: UInt<1>
        input i0: {flip ready: UInt<1>, valid: UInt<1>, data: UInt<32>}
        input i1: {flip ready: UInt<1>, valid: UInt<1>, data: UInt<32>}
        output o0: {flip ready: UInt<1>, valid: UInt<1>, data: UInt<32>}
        ; instances
        inst BITSUB32_mod7_inst of BITSUB32_mod7
        BITSUB32_mod7_inst.clk <= clk
        BITSUB32_mod7_inst.reset <= reset
        BITSUB32_mod7_inst.i0.data <= i0.data
        BITSUB32_mod7_inst.i0.valid <= i0.valid
        BITSUB32_mod7_inst.i1.data <= i1.data
        BITSUB32_mod7_inst.i1.valid <= i1.valid
        o0.data <= BITSUB32_mod7_inst.o0.data
        o0.valid <= BITSUB32_mod7_inst.o0.valid
        BITSUB32_mod7_inst.o0.ready <= and(UInt<1>(1), o0.ready)
        i1.ready <= and(UInt<1>(1), BITSUB32_mod7_inst.i1.ready)
        i0.ready <= and(UInt<1>(1), BITSUB32_mod7_inst.i0.ready)
    module BITSUB32_mod7:
        ; io
        input clk: Clock
        input reset: UInt<1>
        input i0: {flip ready: UInt<1>, valid: UInt<1>, data: UInt<32>}
        input i1: {flip ready: UInt<1>, valid: UInt<1>, data: UInt<32>}
        output o0: {flip ready: UInt<1>, valid: UInt<1>, data: UInt<32>}
        ; registers
        reg i0_valid_reg: UInt<1>, clk with: (reset => (reset, UInt<1>(0)))
        reg i0_data_reg: UInt<32>, clk
        reg i1_valid_reg: UInt<1>, clk with: (reset => (reset, UInt<1>(0)))
        reg i1_data_reg: UInt<32>, clk
        ; logic
        o0.data <=sub(i0_data_reg, i1_data_reg) ; BITSUB32
        ; handshaking
        i0.ready <= not(i0_valid_reg)
        i1.ready <= not(i1_valid_reg)
        node inputs_valid = and(and(UInt<1>(1), i0_valid_reg), i1_valid_reg)
        o0.valid <= and(o0.ready, inputs_valid)
        when o0.valid:
            i0_valid_reg <= UInt<1>(0)
            i1_valid_reg <= UInt<1>(0)
        when i0.valid:
            i0_valid_reg <= UInt<1>(1)
            i0_data_reg <= i0.data
        when i1.valid:
            i1_valid_reg <= UInt<1>(1)
            i1_data_reg <= i1.data
    module test_lambda_mod:
        ; io
        input clk: Clock
        input reset: UInt<1>
        input i: {flip ready: UInt<1>, valid: UInt<1>, data0: UInt<32>, data1: UInt<32>, data2: UInt<1>, data3: UInt<1>, data4: UInt<1>}
        output o: {flip ready: UInt<1>, valid: UInt<1>, data0: UInt<32>, data1: UInt<1>, data2: UInt<1>, data3: UInt<1>}
        ; registers
        reg o0_valid_reg: UInt<1>, clk with: (reset => (reset, UInt<1>(0)))
        reg o0_data_reg: UInt<32>, clk
        reg o1_valid_reg: UInt<1>, clk with: (reset => (reset, UInt<1>(0)))
        reg o1_data_reg: UInt<1>, clk
        reg o2_valid_reg: UInt<1>, clk with: (reset => (reset, UInt<1>(0)))
        reg o2_data_reg: UInt<1>, clk
        reg o3_valid_reg: UInt<1>, clk with: (reset => (reset, UInt<1>(0)))
        reg o3_data_reg: UInt<1>, clk
        ; instances
        inst sr of subregion_mod0
        sr.clk <= clk
        sr.reset <= reset
        sr.i0.valid <= i.valid
        sr.i0.data <= i.data0
        sr.i1.valid <= i.valid
        sr.i1.data <= i.data1
        sr.i2.valid <= i.valid
        sr.i2.data <= i.data2
        sr.i3.valid <= i.valid
        sr.i3.data <= i.data3
        sr.i4.valid <= i.valid
        sr.i4.data <= i.data4
        ; logic
        sr.o0.ready <= not(o0_valid_reg)
        sr.o1.ready <= not(o1_valid_reg)
        sr.o2.ready <= not(o2_valid_reg)
        sr.o3.ready <= not(o3_valid_reg)
        when sr.o0.valid:
            o0_valid_reg <= UInt<1>(1)
            o0_data_reg <= sr.o0.data
        when sr.o1.valid:
            o1_valid_reg <= UInt<1>(1)
            o1_data_reg <= sr.o1.data
        when sr.o2.valid:
            o2_valid_reg <= UInt<1>(1)
            o2_data_reg <= sr.o2.data
        when sr.o3.valid:
            o3_valid_reg <= UInt<1>(1)
            o3_data_reg <= sr.o3.data
        node outputs_valid = and(and(and(and(UInt<1>(1), o0_valid_reg), o1_valid_reg), o2_valid_reg), o3_valid_reg)
        o.valid <= outputs_valid
        o.data0 <= sr.o0.data
        o.data1 <= sr.o1.data
        o.data2 <= sr.o2.data
        o.data3 <= sr.o3.data
        when and(o.valid, o.ready):
            o0_valid_reg <= UInt<1>(0)
            o1_valid_reg <= UInt<1>(0)
            o2_valid_reg <= UInt<1>(0)
            o3_valid_reg <= UInt<1>(0)
        node inputs_ready = and(and(and(and(UInt<1>(1), sr.i0.ready), sr.i1.ready), sr.i2.ready), sr.i3.ready)
        i.ready <= inputs_ready
