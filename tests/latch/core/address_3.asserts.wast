       (assert_return (invoke "64_good1" (i32.const 0)) (f64.const 0.0))
       (assert_return (invoke "64_good2" (i32.const 0)) (f64.const 0.0))
       (assert_return (invoke "64_good3" (i32.const 0)) (f64.const 0.0))
       (assert_return (invoke "64_good4" (i32.const 0)) (f64.const 0.0))
       (assert_return (invoke "64_good1" (i32.const 65510)) (f64.const 0.0))
       (assert_return (invoke "64_good2" (i32.const 65510)) (f64.const 0.0))
       (assert_return (invoke "64_good3" (i32.const 65510)) (f64.const 0.0))
       (assert_return (invoke "64_good4" (i32.const 65510)) (f64.const 0.0))
       (assert_return (invoke "64_good5" (i32.const 65510)) (f64.const 0.0))
       (assert_return (invoke "64_good1" (i32.const 65511)) (f64.const 0.0))
       (assert_return (invoke "64_good2" (i32.const 65511)) (f64.const 0.0))
       (assert_return (invoke "64_good3" (i32.const 65511)) (f64.const 0.0))
       (assert_return (invoke "64_good4" (i32.const 65511)) (f64.const 0.0))