//go:build !windows

package interp

import "testing"

func TestThreadSpawnJoin(t *testing.T) {
	src := `import "fmt"; import "thread";
fn worker() -> i32 {
	return 42;
}
fn main() -> i32 {
	Thread th, err e1 = thread.spawn(worker);
	i32 result, err e2 = th.join();
	fmt.print(string(e1));
	fmt.print(string(e2));
	fmt.print(string(result));
	return 0;
}`
	_, out, err := evalBASL(src)
	if err != nil {
		t.Fatal(err)
	}
	if out[0] != "ok" || out[1] != "ok" || out[2] != "42" {
		t.Fatalf("got %v", out)
	}
}

func TestThreadSleep(t *testing.T) {
	src := `import "fmt"; import "thread"; import "time";
fn main() -> i32 {
	i64 before = time.now();
	thread.sleep(50);
	i64 after = time.now();
	fmt.print(string(after - before >= i64(10)));
	return 0;
}`
	_, out, err := evalBASL(src)
	if err != nil {
		t.Fatal(err)
	}
	if out[0] != "true" {
		t.Fatalf("got %q", out[0])
	}
}

func TestThreadSpawnJoinArgs(t *testing.T) {
	src := `import "fmt"; import "thread";
fn add3(i32 a, i32 b, i32 c) -> i32 {
	return a + b + c;
}
fn main() -> i32 {
	Thread th, err e1 = thread.spawn(add3, 10, 20, 30);
	i32 result, err e2 = th.join();
	fmt.print(string(e1));
	fmt.print(string(e2));
	fmt.print(string(result));
	return 0;
}`
	_, out, err := evalBASL(src)
	if err != nil {
		t.Fatal(err)
	}
	checkOutput(t, out, []string{"ok", "ok", "60"})
}

func TestThreadJoinTwice(t *testing.T) {
	src := `import "fmt"; import "thread";
fn worker() -> i32 {
	return 7;
}
fn main() -> i32 {
	Thread th, err e1 = thread.spawn(worker);
	i32 first, err e2 = th.join();
	i32 _, err e3 = th.join();
	fmt.print(string(e1));
	fmt.print(string(first));
	fmt.print(string(e2 == ok));
	fmt.print(string(e3 != ok));
	return 0;
}`
	_, out, err := evalBASL(src)
	if err != nil {
		t.Fatal(err)
	}
	checkOutput(t, out, []string{"ok", "7", "true", "true"})
}

func TestThreadCallbackSlotsExhaustAndRecycle(t *testing.T) {
	src := `import "fmt"; import "thread";
fn sleeper(i32 ms) -> i32 {
	thread.sleep(ms);
	return ms;
}
fn main() -> i32 {
	Thread t1, err e1 = thread.spawn(sleeper, 20);
	Thread t2, err e2 = thread.spawn(sleeper, 20);
	Thread t3, err e3 = thread.spawn(sleeper, 20);
	Thread t4, err e4 = thread.spawn(sleeper, 20);
	Thread t5, err e5 = thread.spawn(sleeper, 20);
	Thread t6, err e6 = thread.spawn(sleeper, 20);
	Thread t7, err e7 = thread.spawn(sleeper, 20);
	Thread t8, err e8 = thread.spawn(sleeper, 20);
	Thread t9, err e9 = thread.spawn(sleeper, 20);

	if (e1 == ok && e2 == ok && e3 == ok && e4 == ok && e5 == ok && e6 == ok && e7 == ok && e8 == ok) {
		fmt.print("spawned");
	}
	fmt.print(string(e9 != ok));

	i32 _, err j1 = t1.join();
	i32 _, err j2 = t2.join();
	i32 _, err j3 = t3.join();
	i32 _, err j4 = t4.join();
	i32 _, err j5 = t5.join();
	i32 _, err j6 = t6.join();
	i32 _, err j7 = t7.join();
	i32 _, err j8 = t8.join();

	if (j1 == ok && j2 == ok && j3 == ok && j4 == ok && j5 == ok && j6 == ok && j7 == ok && j8 == ok) {
		fmt.print("joined");
	}

	Thread t10, err e10 = thread.spawn(sleeper, 1);
	i32 r10, err j10 = t10.join();
	fmt.print(string(e10));
	fmt.print(string(j10));
	fmt.print(string(r10));
	return 0;
}`
	_, out, err := evalBASL(src)
	if err != nil {
		t.Fatal(err)
	}
	checkOutput(t, out, []string{"spawned", "true", "joined", "ok", "ok", "1"})
}

func TestThreadSharedStateWithMutex(t *testing.T) {
	src := `import "fmt"; import "thread"; import "mutex";
class Counter {
	i32 n;
	fn init() -> void {
		self.n = 0;
	}
}
fn worker(Counter c, Mutex m, i32 times) -> i32 {
	for (i32 i = 0; i < times; i++) {
		err e1 = m.lock();
		c.n++;
		err e2 = m.unlock();
	}
	return times;
}
fn main() -> i32 {
	Counter c = Counter();
	Mutex m, err me = mutex.new();
	Thread t1, err e1 = thread.spawn(worker, c, m, 25);
	Thread t2, err e2 = thread.spawn(worker, c, m, 25);
	Thread t3, err e3 = thread.spawn(worker, c, m, 25);
	Thread t4, err e4 = thread.spawn(worker, c, m, 25);

	i32 r1, err j1 = t1.join();
	i32 r2, err j2 = t2.join();
	i32 r3, err j3 = t3.join();
	i32 r4, err j4 = t4.join();
	err md = m.destroy();

	if (me == ok && e1 == ok && e2 == ok && e3 == ok && e4 == ok && j1 == ok && j2 == ok && j3 == ok && j4 == ok && md == ok) {
		fmt.print("ok");
	}
	fmt.print(string(r1 + r2 + r3 + r4));
	fmt.print(string(c.n));
	return 0;
}`
	_, out, err := evalBASL(src)
	if err != nil {
		t.Fatal(err)
	}
	checkOutput(t, out, []string{"ok", "100", "100"})
}

func TestMutexLockUnlock(t *testing.T) {
	src := `import "fmt"; import "mutex";
fn main() -> i32 {
	Mutex m, err e1 = mutex.new();
	err e2 = m.lock();
	err e3 = m.unlock();
	err e4 = m.destroy();
	fmt.print(string(e1));
	fmt.print(string(e2));
	fmt.print(string(e3));
	fmt.print(string(e4));
	return 0;
}`
	_, out, err := evalBASL(src)
	if err != nil {
		t.Fatal(err)
	}
	if out[0] != "ok" || out[1] != "ok" || out[2] != "ok" || out[3] != "ok" {
		t.Fatalf("got %v", out)
	}
}

func TestMutexDestroyTwice(t *testing.T) {
	src := `import "fmt"; import "mutex";
fn main() -> i32 {
	Mutex m, err e1 = mutex.new();
	err e2 = m.destroy();
	err e3 = m.destroy();
	fmt.print(string(e1));
	fmt.print(string(e2 == ok));
	fmt.print(string(e3 != ok));
	return 0;
}`
	_, out, err := evalBASL(src)
	if err != nil {
		t.Fatal(err)
	}
	checkOutput(t, out, []string{"ok", "true", "true"})
}

func TestMutexLockAfterDestroy(t *testing.T) {
	src := `import "fmt"; import "mutex";
fn main() -> i32 {
	Mutex m, err e1 = mutex.new();
	err e2 = m.destroy();
	err e3 = m.lock();
	fmt.print(string(e1));
	fmt.print(string(e2 == ok));
	fmt.print(string(e3 != ok));
	fmt.print(e3.message());
	return 0;
}`
	_, out, err := evalBASL(src)
	if err != nil {
		t.Fatal(err)
	}
	checkOutput(t, out, []string{"ok", "true", "true", "mutex is destroyed"})
}

func TestThreadSharedStateWithMutexStress(t *testing.T) {
	src := `import "fmt"; import "thread"; import "mutex";
class Counter {
	i32 n;
	fn init() -> void {
		self.n = 0;
	}
}
fn worker(Counter c, Mutex m, i32 times) -> i32 {
	for (i32 i = 0; i < times; i++) {
		err e1 = m.lock();
		c.n++;
		err e2 = m.unlock();
	}
	return times;
}
fn main() -> i32 {
	Counter c = Counter();
	Mutex m, err me = mutex.new();
	Thread t1, err e1 = thread.spawn(worker, c, m, 10);
	Thread t2, err e2 = thread.spawn(worker, c, m, 10);
	Thread t3, err e3 = thread.spawn(worker, c, m, 10);
	Thread t4, err e4 = thread.spawn(worker, c, m, 10);

	i32 r1, err j1 = t1.join();
	i32 r2, err j2 = t2.join();
	i32 r3, err j3 = t3.join();
	i32 r4, err j4 = t4.join();
	err md = m.destroy();

	if (me == ok && e1 == ok && e2 == ok && e3 == ok && e4 == ok && j1 == ok && j2 == ok && j3 == ok && j4 == ok && md == ok) {
		fmt.print("ok");
	}
	fmt.print(string(r1 + r2 + r3 + r4));
	fmt.print(string(c.n));
	return 0;
}`
	for i := 0; i < 100; i++ {
		_, out, err := evalBASL(src)
		if err != nil {
			t.Fatalf("iteration %d: %v", i, err)
		}
		want := []string{"ok", "40", "40"}
		for j, w := range want {
			if j >= len(out) || out[j] != w {
				t.Fatalf("iteration %d: output[%d] = %q, want %q (full: %v)", i, j, safeIdx(out, j), w, out)
			}
		}
	}
}

func TestGILStdlibFromThreads(t *testing.T) {
	// Multiple threads calling stdlib functions concurrently.
	// Without the GIL this would race on interpreter internals.
	src := `import "fmt"; import "thread"; import "math"; import "strings";
fn worker(i32 id) -> string {
	f64 v = math.sqrt(f64(id * id));
	string s = fmt.sprintf("t%d=%.0f", id, v);
	return s;
}
fn main() -> i32 {
	Thread t1, err e1 = thread.spawn(worker, 3);
	Thread t2, err e2 = thread.spawn(worker, 4);
	Thread t3, err e3 = thread.spawn(worker, 5);
	string r1, err j1 = t1.join();
	string r2, err j2 = t2.join();
	string r3, err j3 = t3.join();
	fmt.print(r1);
	fmt.print(r2);
	fmt.print(r3);
	fmt.print(string(e1 == ok && e2 == ok && e3 == ok));
	fmt.print(string(j1 == ok && j2 == ok && j3 == ok));
	return 0;
}`
	for i := 0; i < 50; i++ {
		_, out, err := evalBASL(src)
		if err != nil {
			t.Fatalf("iteration %d: %v", i, err)
		}
		checkOutput(t, out, []string{"t3=3", "t4=4", "t5=5", "true", "true"})
	}
}
