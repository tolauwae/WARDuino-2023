       (assert_return (invoke "copysign" (f32.const -0x0p+0) (f32.const 0x0p+0)) (f32.const 0x0p+0))
       (assert_return (invoke "copysign" (f32.const 0x0p+0) (f32.const 0x0p+0)) (f32.const 0x0p+0))
       (assert_return (invoke "copysign" (f32.const -0x0p+0) (f32.const 0x1p-149)) (f32.const 0x0p+0))
       (assert_return (invoke "copysign" (f32.const 0x0p+0) (f32.const 0x1p-149)) (f32.const 0x0p+0))
       (assert_return (invoke "copysign" (f32.const -0x0p+0) (f32.const 0x1p-126)) (f32.const 0x0p+0))
       (assert_return (invoke "copysign" (f32.const 0x0p+0) (f32.const 0x1p-126)) (f32.const 0x0p+0))
       (assert_return (invoke "copysign" (f32.const -0x0p+0) (f32.const 0x1p+0)) (f32.const 0x0p+0))
       (assert_return (invoke "copysign" (f32.const 0x0p+0) (f32.const 0x1p+0)) (f32.const 0x0p+0))
       (assert_return (invoke "copysign" (f32.const -0x0p+0) (f32.const 0x1.921fb6p+2)) (f32.const 0x0p+0))
       (assert_return (invoke "copysign" (f32.const 0x0p+0) (f32.const 0x1.921fb6p+2)) (f32.const 0x0p+0))
       (assert_return (invoke "copysign" (f32.const -0x0p+0) (f32.const 0x1.fffffep+127)) (f32.const 0x0p+0))
       (assert_return (invoke "copysign" (f32.const 0x0p+0) (f32.const 0x1.fffffep+127)) (f32.const 0x0p+0))
       (assert_return (invoke "abs" (f32.const -0x0p+0)) (f32.const 0x0p+0))
       (assert_return (invoke "abs" (f32.const 0x0p+0)) (f32.const 0x0p+0))
