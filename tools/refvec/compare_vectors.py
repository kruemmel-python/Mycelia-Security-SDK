import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
VECTORS = ROOT / "tests" / "vectors" / "generated_vectors.json"


def run_cmd(cmd):
    result = subprocess.run(cmd, check=True, capture_output=True, text=True)
    return result.stdout.strip()


def hex_diff(expected, actual):
    for i in range(0, min(len(expected), len(actual)), 2):
        if expected[i:i+2] != actual[i:i+2]:
            return i // 2, expected[i:i+2], actual[i:i+2]
    if len(expected) != len(actual):
        return min(len(expected), len(actual)) // 2, expected[min(len(expected), len(actual)):] or "--", actual[min(len(expected), len(actual)):] or "--"
    return None


def main():
    if not VECTORS.exists():
        print(f"Missing vectors file: {VECTORS}")
        sys.exit(1)

    shader_dir = sys.argv[1] if len(sys.argv) > 1 else "./shaders"
    data = json.loads(VECTORS.read_text())
    failures = 0

    for idx, vec in enumerate(data["vectors"]):
        seed = str(vec["seed"])
        offset = str(vec["stream_offset"])
        plaintext = vec["plaintext_hex"]
        expected = vec["ciphertext_hex"]

        cmd = ["./vulkan_runner", shader_dir, seed, offset, plaintext, "enc"]
        actual = run_cmd(cmd)

        if actual != expected:
            diff = hex_diff(expected, actual)
            if diff:
                byte_offset, exp, act = diff
                print(f"Mismatch case {idx} at byte {byte_offset}: expected {exp}, got {act}")
            else:
                print(f"Mismatch case {idx}: output length mismatch")
            failures += 1
        else:
            print(f"Case {idx} OK")

    if failures:
        print(f"Failed {failures} cases")
        sys.exit(1)
    print("All cases matched")


if __name__ == "__main__":
    main()
