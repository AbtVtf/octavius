#!/usr/bin/env python3
"""
Model Benchmark — Claude vs Gemma 4

Runs the same coding tasks on both models and compares:
- Correctness (does it work?)
- Speed (how fast?)
- Code quality (how clean?)
"""

import subprocess
import time
import json
import os
import sys

TASKS = [
    # Level 1: Easy
    {
        "name": "L1: FizzBuzz",
        "prompt": "Write a Python function fizzbuzz(n) that returns a list of strings from 1 to n. For multiples of 3 use 'Fizz', multiples of 5 use 'Buzz', both use 'FizzBuzz', otherwise the number as string. Include 3 test cases using assert.",
        "test": "from solution import fizzbuzz; assert fizzbuzz(15)[-1] == 'FizzBuzz'; assert fizzbuzz(5)[2] == 'Fizz'; assert fizzbuzz(5)[4] == 'Buzz'; print('PASS')",
    },
    # Level 2: Medium
    {
        "name": "L2: LRU Cache",
        "prompt": "Write a Python class LRUCache with __init__(capacity), get(key), and put(key, value). O(1) for both operations. Include 3 test cases using assert.",
        "test": "from solution import LRUCache; c = LRUCache(2); c.put(1,'a'); c.put(2,'b'); assert c.get(1) == 'a'; c.put(3,'c'); assert c.get(2) is None; print('PASS')",
    },
    # Level 3: Hard
    {
        "name": "L3: Trie with Autocomplete",
        "prompt": "Write a Python class Trie with insert(word), search(word) -> bool, starts_with(prefix) -> bool, and autocomplete(prefix) -> list of all words with that prefix. Include 5 test cases using assert.",
        "test": "from solution import Trie; t = Trie(); t.insert('apple'); t.insert('app'); t.insert('banana'); assert t.search('apple'); assert not t.search('appl'); assert sorted(t.autocomplete('app')) == ['app', 'apple']; print('PASS')",
    },
    # Level 4: Expert
    {
        "name": "L4: Async Rate Limiter",
        "prompt": "Write a Python class AsyncRateLimiter using asyncio that allows max N requests per second using a token bucket algorithm. Methods: async acquire() that waits if rate exceeded, and a context manager 'async with limiter:'. Include a test that proves rate limiting works by timing 5 requests with limit=2/sec.",
        "test": "import asyncio; from solution import AsyncRateLimiter; async def test(): l = AsyncRateLimiter(2); t=asyncio.get_event_loop().time(); [await l.acquire() for _ in range(4)]; elapsed=asyncio.get_event_loop().time()-t; assert elapsed >= 1.0, f'Too fast: {elapsed}'; print('PASS'); asyncio.run(test())",
    },
    # Level 5: Beast
    {
        "name": "L5: Mini Regex Engine",
        "prompt": "Write a Python function regex_match(pattern, text) -> bool that supports: '.' (any char), '*' (zero or more of previous), '+' (one or more of previous), '?' (zero or one of previous), character classes [abc], and escaping with backslash. No re module allowed. Include 10 test cases using assert.",
        "test": "from solution import regex_match; assert regex_match('a.c', 'abc'); assert regex_match('a*', 'aaa'); assert not regex_match('a+', ''); assert regex_match('a?b', 'b'); assert regex_match('[abc]', 'b'); print('PASS')",
    },
]


def run_model(model_name, cli_cmd, prompt, work_dir, timeout=120):
    """Run a model on a task, return (code, time, success)."""
    os.makedirs(work_dir, exist_ok=True)

    full_prompt = (
        f"{prompt}\n\n"
        f"Write ONLY the Python code to a file called 'solution.py'. "
        f"No explanation, no markdown, just write the file."
    )

    start = time.time()
    try:
        result = subprocess.run(
            cli_cmd + [full_prompt],
            capture_output=True, text=True, timeout=timeout,
            cwd=work_dir,
        )
        elapsed = time.time() - start

        # Check if solution.py was created
        sol_path = os.path.join(work_dir, "solution.py")
        if os.path.exists(sol_path):
            with open(sol_path) as f:
                code = f.read()
            return code, elapsed, True
        else:
            return result.stdout[-500:] if result.stdout else "No output", elapsed, False

    except subprocess.TimeoutExpired:
        return "TIMEOUT", timeout, False


def test_solution(work_dir, test_code):
    """Run tests on the solution."""
    try:
        result = subprocess.run(
            [sys.executable, "-c", test_code],
            capture_output=True, text=True, timeout=10,
            cwd=work_dir,
        )
        return result.returncode == 0, result.stdout + result.stderr
    except:
        return False, "Test crashed"


if __name__ == "__main__":
    results = []

    # Select which tasks to run (default: all)
    task_indices = list(range(len(TASKS)))
    if len(sys.argv) > 1:
        task_indices = [int(x) for x in sys.argv[1:]]

    for idx in task_indices:
        task = TASKS[idx]
        print(f"\n{'='*60}")
        print(f"TASK: {task['name']}")
        print(f"{'='*60}")

        task_result = {"name": task["name"]}

        for model_name, cli_cmd in [
            ("Claude", ["claude", "-p", "--dangerously-skip-permissions", "--model", "sonnet"]),
            ("Gemma4", ["claude", "-p", "--dangerously-skip-permissions", "--model", "gemma-4-26b"]),
        ]:
            work_dir = f"/tmp/benchmark/{task['name'].replace(' ', '_').replace(':', '')}_{model_name}"

            print(f"\n--- {model_name} ---")
            code, elapsed, created = run_model(model_name, cli_cmd, task["prompt"], work_dir)

            if created:
                print(f"  Generated in {elapsed:.1f}s ({len(code)} chars)")
                passed, test_output = test_solution(work_dir, task["test"])
                print(f"  Tests: {'PASS ✓' if passed else 'FAIL ✗'}")
                if not passed:
                    print(f"  Error: {test_output[:200]}")
                task_result[model_name] = {
                    "time": round(elapsed, 1),
                    "chars": len(code),
                    "passed": passed,
                }
            else:
                print(f"  Failed to generate solution ({elapsed:.1f}s)")
                task_result[model_name] = {"time": round(elapsed, 1), "passed": False}

        results.append(task_result)

    # Summary
    print(f"\n{'='*60}")
    print("SUMMARY")
    print(f"{'='*60}")
    print(f"{'Task':<30} {'Claude':>15} {'Gemma4':>15}")
    print("-" * 60)
    for r in results:
        c = r.get("Claude", {})
        g = r.get("Gemma4", {})
        c_str = f"{'✓' if c.get('passed') else '✗'} {c.get('time', '?')}s"
        g_str = f"{'✓' if g.get('passed') else '✗'} {g.get('time', '?')}s"
        print(f"{r['name']:<30} {c_str:>15} {g_str:>15}")

    # Save results
    with open("/tmp/benchmark/results.json", "w") as f:
        json.dump(results, f, indent=2)
    print(f"\nFull results: /tmp/benchmark/results.json")
