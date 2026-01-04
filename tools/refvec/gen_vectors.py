import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TESTS = ROOT / "tests" / "vectors" / "test_cases.json"
OUT = ROOT / "tests" / "vectors" / "generated_vectors.json"


def run_cmd(cmd):
    result = subprocess.run(cmd, check=True, capture_output=True, text=True)
    return result.stdout.strip()


def main():
    data = json.loads(TESTS.read_text())
    vectors = []
    for case in data["cases"]:
        seed = str(case["seed"])
        offset = str(case["stream_offset"])
        plaintext_hex = case["plaintext_hex"]
        gpu_index = str(case.get("gpu_index", 0))
        cmd = ["./refvec_cli", seed, offset, plaintext_hex, gpu_index]
        ciphertext_hex = run_cmd(cmd)
        vectors.append({
            "seed": case["seed"],
            "stream_offset": case["stream_offset"],
            "plaintext_hex": plaintext_hex,
            "ciphertext_hex": ciphertext_hex,
        })
    OUT.write_text(json.dumps({"vectors": vectors}, indent=2))
    print(f"Wrote {OUT}")


if __name__ == "__main__":
    main()
