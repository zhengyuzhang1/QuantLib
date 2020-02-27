import fileinput
import os
import sys

replace_dict = {
    "ql/shared_ptr.hpp": "memory",
    "ql/functional.hpp": "functional",
    "QL_UNIQUE_OR_AUTO_PTR": "std::unique_ptr",
    "QuantLib::ext": "std",
    "ext::": "std::",
    "boost/optional.hpp": "optional",
    "boost::optional": "std::optional",
    "boost::none": "std::nullopt",
    " none;": " std::nullopt;",
    "boost/unordered_set.hpp": "unordered_set",
    "boost::unordered_set": "std::unordered_set",
    # replace boost::tuple with std::tuple
    "boost/tuple/tuple.hpp": "tuple",
    "boost::tuples::tuple": "std::tuple",
    "boost::tuple": "std::tuple",
    "boost::make_tuple": "std::make_tuple",
}

removed_line = ["ql/auto_ptr.hpp", "using boost::none"]

macro_def = {
    "QL_USE_STD_UNIQUE_PTR": 1,
    "QL_USE_STD_SHARED_PTR": 1,
    "QL_USE_STD_FUNCTION": 1,
    "QL_ENABLE_SINGLETON_THREAD_SAFE_INIT": 1,
    "QL_ENABLE_SESSIONS": 0,
}


def macro_str(key: str, value) -> str:
    if value == 1:
        return "-D " + key
    elif value == 0:
        return "-U " + key
    else:
        return "-D " + key + "=" + value


def_argument = " ".join(macro_str(old, new) for old, new in macro_def.items())


def remove_ifdef(fname) -> None:
    command_str = "unifdef -m " + def_argument + " " + fname
    print(command_str)
    os.system(command_str)


class Add_guard:
    def __init__(self, path):
        self.path_ = path
        with open(path, "r") as header:
            self.lines_ = header.readlines()
        self.macro_lines_ = [
            line.strip() for line in self.lines_ if self.is_macro(line)
        ]

    def is_macro(self, line: str) -> bool:
        return line.strip().startswith("#")

    def get_macro(self, index: int) -> str:
        return self.macro_lines_[index] if index < len(self.macro_lines_) else ""

    def has_guard(self) -> bool:
        macro1 = self.get_macro(0)
        macro2 = self.get_macro(1)
        macro3 = self.get_macro(-1)
        return (
            macro1.startswith("#ifndef")
            and macro2.startswith("#define")
            and macro3 == "#endif"
            and macro1.split()[1] == macro2.split()[1]
        )

    def first_non_comment_blank_index(self) -> int:
        in_block_comment = False
        for i, line in enumerate(self.lines_):
            s_line = line.strip()
            if not s_line:
                continue
            if not s_line.startswith("/") and not in_block_comment:
                return i
            if s_line.startswith("/*"):
                in_block_comment = True
            if s_line.endswith("*/"):
                in_block_comment = False

    def add_guard(self) -> bool:
        if self.has_guard():
            return False

        guard_str = self.path_[2:].replace("/", "_").replace(".", "_").upper()
        guard_macro = []
        guard_macro.append(
            ["#ifndef " + guard_str + "\n", "#define " + guard_str + "\n", "\n"]
        )
        guard_macro.append(["\n", "#endif\n"])
        insert_index = self.first_non_comment_blank_index()
        new_lines = (
            self.lines_[:insert_index]
            + guard_macro[0]
            + self.lines_[insert_index:]
            + guard_macro[1]
        )
        with open(self.path_, "w") as header:
            header.writelines(new_lines)
        return True


def process(line: str) -> None:
    if any(head in line for head in removed_line):
        return
    for old_str in replace_dict:
        line = line.replace(old_str, replace_dict[old_str])
    sys.stdout.write(line)


for dirpath, dirs, files in os.walk("."):
    for filename in files:
        if filename.endswith(".cpp") or filename.endswith(".hpp"):
            fname = os.path.join(dirpath, filename)
            remove_ifdef(fname)
            if filename == "all.hpp":
                Add_guard(fname).add_guard()
            for line in fileinput.input(fname, inplace=True):
                process(line)
