"""Write the pyarrow dictionary fixture the C++ reader test consumes."""
import pathlib

import pyarrow as pa
import pyarrow.ipc as ipc

fixtures = pathlib.Path(__file__).resolve().parent.parent / "test" / "fixtures"
fixtures.mkdir(parents=True, exist_ok=True)

# Two distinct values: pyarrow keeps the array it is given as the dictionary,
# duplicates included, so a repeated entry would make the dictionary 3 long.
values = pa.array(["ozow", "payfast"])
dictionary = pa.DictionaryArray.from_arrays(
    pa.array([0, 1, 0], type=pa.int32()), values)
table = pa.table({"merchant": dictionary})

path = fixtures / "dict_pyarrow.arrows"
with path.open("wb") as handle:
    with ipc.new_stream(handle, table.schema) as writer:
        writer.write_table(table)

print(f"OK: wrote {path} with {table.num_rows} rows")
