import os
import sys
import json
import time
import subprocess
from pathlib import Path
from openai import OpenAI

# Initialize client using environment variables
API_KEY = os.getenv("AI_API_KEY") or os.getenv("OPENAI_API_KEY")
BASE_URL = os.getenv("AI_BASE_URL", "https://api.openai.com/v1")
MODEL_NAME = os.getenv("AI_MODEL", "gpt-4o")

if not API_KEY:
    print("Error: AI_API_KEY or OPENAI_API_KEY environment variable is missing.")
    sys.exit(1)

client = OpenAI(api_key=API_KEY, base_url=BASE_URL)

# Dynamic workspace path detection (supports both /workspace/auracash and /root/auracash)
WORKSPACE_DIR = Path(os.getenv("WORKSPACE_DIR", Path(__file__).parent.resolve())).resolve()

# --- Tool Implementations ---

def execute_bash(command: str) -> str:
    """Executes a bash command inside the container or host workspace directory."""
    try:
        result = subprocess.run(
            command,
            shell=True,
            cwd=WORKSPACE_DIR,
            capture_output=True,
            text=True,
            timeout=300
        )
        output = f"Exit Code: {result.returncode}\n"
        
        stdout = result.stdout or ""
        stderr = result.stderr or ""

        # Truncate output if too long to save context window tokens, keeping stdout tail and stderr head/tail
        if len(stdout) > 6000:
            stdout = stdout[:1000] + "\n... [TRUNCATED STDOUT] ...\n" + stdout[-5000:]
        if len(stderr) > 6000:
            stderr = stderr[:1000] + "\n... [TRUNCATED STDERR] ...\n" + stderr[-5000:]

        if stdout:
            output += f"STDOUT:\n{stdout}\n"
        if stderr:
            output += f"STDERR:\n{stderr}\n"
            
        return output
    except subprocess.TimeoutExpired:
        return "Error: Command timed out after 300 seconds."
    except Exception as e:
        return f"Execution Error: {str(e)}"

def read_file(filepath: str) -> str:
    """Reads content from a file relative to the workspace."""
    try:
        target_path = (WORKSPACE_DIR / filepath).resolve()
        if not str(target_path).startswith(str(WORKSPACE_DIR)):
            return "Error: Path traversal outside workspace denied."
        if not target_path.exists():
            return f"Error: File {filepath} does not exist."
        
        content = target_path.read_text(encoding="utf-8")
        if len(content) > 12000:
            return content[:2000] + f"\n\n... [TRUNCATED {len(content)-4000} BYTES] ...\n\n" + content[-2000:]
        return content
    except Exception as e:
        return f"Read Error: {str(e)}"

def write_file(filepath: str, content: str) -> str:
    """Writes or overwrites content to a file in the workspace."""
    try:
        target_path = (WORKSPACE_DIR / filepath).resolve()
        if not str(target_path).startswith(str(WORKSPACE_DIR)):
            return "Error: Path traversal outside workspace denied."
        target_path.parent.mkdir(parents=True, exist_ok=True)
        target_path.write_text(content, encoding="utf-8")
        return f"Successfully wrote {len(content)} bytes to {filepath}."
    except Exception as e:
        return f"Write Error: {str(e)}"

def list_files(directory: str = ".") -> str:
    """Lists files and directories inside the given path."""
    try:
        target_path = (WORKSPACE_DIR / directory).resolve()
        if not str(target_path).startswith(str(WORKSPACE_DIR)):
            return "Error: Path traversal outside workspace denied."
        items = []
        for p in target_path.rglob("*"):
            if ".git" in p.parts or "build" in p.parts:
                continue
            rel_path = p.relative_to(WORKSPACE_DIR)
            items.append(f"{'[DIR]' if p.is_dir() else '[FILE]'} {rel_path}")
        return "\n".join(items) if items else "Directory is empty."
    except Exception as e:
        return f"List Error: {str(e)}"

# --- OpenAI Tool Definitions ---

tools = [
    {
        "type": "function",
        "function": {
            "name": "execute_bash",
            "description": "Execute terminal commands in the workspace (e.g., build C++ code, run tests, install packages, compile CMake).",
            "parameters": {
                "type": "object",
                "properties": {
                    "command": {"type": "string", "description": "The exact bash command to execute."}
                },
                "required": ["command"]
            }
        }
    },
    {
        "type": "function",
        "function": {
            "name": "read_file",
            "description": "Read content from a workspace file.",
            "parameters": {
                "type": "object",
                "properties": {
                    "filepath": {"type": "string", "description": "Relative file path from workspace root."}
                },
                "required": ["filepath"]
            }
        }
    },
    {
        "type": "function",
        "function": {
            "name": "write_file",
            "description": "Write or overwrite content to a file in the workspace.",
            "parameters": {
                "type": "object",
                "properties": {
                    "filepath": {"type": "string", "description": "Relative file path."},
                    "content": {"type": "string", "description": "Full file content to write."}
                },
                "required": ["filepath", "content"]
            }
        }
    },
    {
        "type": "function",
        "function": {
            "name": "list_files",
            "description": "List files and directories in the workspace.",
            "parameters": {
                "type": "object",
                "properties": {
                    "directory": {"type": "string", "description": "Relative directory path. Defaults to root '.'"}
                }
            }
        }
    }
]

SYSTEM_PROMPT = """You are an expert Principal Blockchain Engineer and Core Developer tasked with completing, fixing, and verifying the full AuraCash (XAC) network ecosystem.

DIAGNOSTIC & BUILD RULE:
If a compilation or CMake build error occurs (e.g., `[Makefile:146: all] Error 2`), immediately inspect stderr, clean stale build caches (`rm -rf build`), inspect the failing C++ source code, fix compiler/type errors, and rebuild (`cmake -B build && cmake --build build -j$(nproc)`).

YOUR MANDATE & MANDATORY IMPLEMENTATION TASKS:

1. FIX COMPILATION & RPC CODE IN NODE (`auracash-node`):
   - Fix all compiler errors in `src/rpc/` and `src/node/`.
   - Fix `getblocktemplate` JSON output. Ensure `bits` and `coinbasevalue` are serialized as integers:
     `response["result"]["bits"] = static_cast<uint32_t>(bits);`
     `response["result"]["coinbasevalue"] = static_cast<uint64_t>(coinbasevalue);`
   - Ensure `getblocktemplate` returns all required fields: `previousblockhash`, `merkleroot`, `coinbasevalue`, `bits`, `height`, and `target`.
   - Register and fully implement all required JSON-RPC 2.0 endpoints:
     * `getblocktemplate`
     * `submitblock`
     * `getblockchaininfo`
     * `sendrawtransaction`
     * `getbalance`

2. FIX STANDALONE MINER (`auracash-miner`):
   - Update `Miner.cpp` JSON parsing logic to cleanly parse `bits` and `coinbasevalue` integers without throwing float parsing exceptions.
   - Update CLI argument parsing to handle positional inputs (`./auracash-miner [host] [port] [address]`) or flag inputs (`--node` / `--address`).
   - Update coinbase transaction construction to use the mining wallet address passed via arguments.

3. AUTOMATIC P2P PEER DISCOVERY:
   - Configure `auracash-node` to automatically attempt connection to seed nodes on startup (`seed1.auracash.org:8333`, `seed2.auracash.org:8333`).
   - Ensure fallback to local p2p discovery so external nodes sync automatically.

4. CREATE SYSTEM INSTALLER & CLI WRAPPER (`install.sh` & `auracash` binary):
   - Ensure `install.sh` cleans stale CMake cache (`rm -rf build`) before running CMake.
   - Copy compiled binaries (`auracash-node`, `auracash-wallet`, `auracash-miner`) to `/usr/local/bin/`.
   - Create `/usr/local/bin/auracash` CLI script supporting `auracash node`, `auracash miner`, `auracash wallet generatekey`, and `auracash wallet getbalance`.

5. END-TO-END VERIFICATION:
   - Run clean build, launch node daemon, execute RPC calls, generate wallet keys, mine block, verify balance update, and pass `ctest --output-on-failure`.
"""

def call_openai_with_retry(messages, max_retries=3):
    """Executes API requests with exponential backoff on transient errors."""
    for attempt in range(max_retries):
        try:
            return client.chat.completions.create(
                model=MODEL_NAME,
                messages=messages,
                tools=tools,
                tool_choice="auto"
            )
        except Exception as e:
            print(f"[Warning] API Call Attempt {attempt + 1} failed: {e}")
            if attempt == max_retries - 1:
                raise e
            time.sleep(2 ** (attempt + 1))

def run_agent():
    print(f"=== Starting Autonomous AI Builder for Complete AuraCash Network ===")
    print(f"Workspace: {WORKSPACE_DIR}")
    print(f"Model: {MODEL_NAME}")
    print(f"Endpoint: {BASE_URL}")
    print("===========================================================================")

    messages = [
        {"role": "system", "content": SYSTEM_PROMPT},
        {"role": "user", "content": "A build error occurred (Makefile Error 2). Diagnose the build output, fix all C++ source code compilation errors, fix integer JSON output, update miner argument parsing, implement seed node auto-discovery, build the install.sh CLI tool, and run end-to-end tests."}
    ]

    iteration = 0
    while True:
        iteration += 1
        print(f"\n--- [Iteration {iteration}] Calling AI Model ---")
        
        try:
            response = call_openai_with_retry(messages)
        except Exception as e:
            print(f"API Error: {e}")
            break

        message = response.choices[0].message
        messages.append(message)

        if message.content:
            print(f"\n[AI Assistant Output]:\n{message.content}")

        if not message.tool_calls:
            print("\n=== AI agent signaled execution complete (no more tool calls) ===")
            break

        for tool_call in message.tool_calls:
            func_name = tool_call.function.name
            args = json.loads(tool_call.function.arguments)
            call_id = tool_call.id

            print(f"\n[Tool Execution]: {func_name}")
            print(f"Arguments: {json.dumps(args, indent=2)}")

            if func_name == "execute_bash":
                result = execute_bash(args.get("command", ""))
            elif func_name == "read_file":
                result = read_file(args.get("filepath", ""))
            elif func_name == "write_file":
                result = write_file(args.get("filepath", ""), args.get("content", ""))
            elif func_name == "list_files":
                result = list_files(args.get("directory", "."))
            else:
                result = f"Unknown tool: {func_name}"

            print(f"Result Snippet: {result[:300]}...")

            messages.append({
                "role": "tool",
                "tool_call_id": call_id,
                "content": result
            })

if __name__ == "__main__":
    run_agent()
