FastStreamCompute is a C++ engine that takes a small numerical computation, prepares it once into a reusable execution plan, and runs that plan repeatedly over input records.

Initially, I will describe computations through a small C++ builder interface. The builder will describe what operations to perform, rather than immediately performing the calculation. StreamExec will check that description and prepare it for execution.

My first example will calculate midpoint = (bid + ask) * 0.5. Each input record will contain a bid and an ask; a record containing 100 and 102 will produce 101. The midpoint is only the first example: the same engine should also execute other supported computations, such as spread = ask - bid, without requiring a new execution loop.

I will begin with a straightforward evaluator whose behaviour is easy to understand and test. Later, I will run the same computations through alternative execution backends, starting with register bytecode, and compare their correctness, execution cost and memory use.

The central question is: What does programmability cost compared with directly writing a specialised C++ calculation, and which design choices reduce that overhead?

The first version will process records already in memory. File replay and network input can be added later as different ways of supplying records to the same engine. The core remains the execution system—not the network service, parser or trading example.