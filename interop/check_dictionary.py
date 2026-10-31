"""pyarrow must accept the dictionary stream the C++ writer produced."""
import argparse
import pathlib
import sys

import pyarrow as pa
import pyarrow.ipc as ipc

parser = argparse.ArgumentParser()
parser.add_argument(
    "--path",
    default=str(pathlib.Path(__file__).resolve().parent.parent / "build" / "dict_cpp.arrows"),
)
args = parser.parse_args()
path = pathlib.Path(args.path)
if not path.exists():
    sys.exit(f"error: {path} is missing, run ./build/dictionary_test.exe first")

with path.open("rb") as handle:
    table = ipc.open_stream(handle).read_all()

field = table.schema.field("merchant")
assert field.type == pa.dictionary(pa.int32(), pa.string()), field.type
assert not field.type.ordered, "the dictionary must be unordered"
assert table.num_rows == 3, table.num_rows
values = table.column("merchant").to_pylist()
assert values == ["ozow", "payfast", "ozow"], values
print(f"OK: pyarrow read {table.num_rows} dictionary values from the C++ writer")
