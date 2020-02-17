import fileinput
import os
import sys

replace_dict = {
    "ql/shared_ptr.hpp": "memory",
    "ql/functional.hpp": "functional",
    "QL_UNIQUE_OR_AUTO_PTR": "std::unique_ptr",
    "ext::": "std::",
}

removed_head = ["auto_ptr.hpp"]


def remove_ifdef(fname) -> None:
    macro_def = {
        "QL_USE_STD_UNIQUE_PTR": 1,
        "QL_USE_STD_SHARED_PTR": 1,
        "QL_USE_STD_FUNCTION": 1,
    }
    def_argument = " ".join(
        "-D" + old + ("=" + new if new != 1 else "") for old, new in macro_def.items()
    )
    os.system("unifdef -M bak " + def_argument + fname)


def process(line: str) -> None:
    if any(head in line for head in removed_head):
        return
    for old_str in replace_dict:
        line.replace(old_str, replace_dict[old_str])
        sys.stdout.write(line)


for dirpath, dirs, files in os.walk("."):
    for filename in files:
        if filename.endswith(".cpp") or filename.endswith(".hpp"):
            fname = os.path.join(dirpath, filename)
            remove_ifdef(fname)
            for line in fileinput.input(fname, inplace=True):
                process(line)
