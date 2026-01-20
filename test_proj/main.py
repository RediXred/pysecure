from second import greet
from dir.subdirr import (
    SubdirClass,
    ChildSubdirClass,
    AdvancedSubdir,
    ExpertSubdir
)
from dir.subdir.subpy import (
    subpy_function,
    SubPyClass
)

def main():
    print("=== Запуск main.py ===\n")

    greet("User")

    # Базовый класс
    base = SubdirClass("Сообщение из SubdirClass")
    base.show_message()
    print()

    # Наследник
    child = ChildSubdirClass("Сообщение из ChildSubdirClass")
    child.show_message()
    child.extra_message()
    print()

    # Сложный класс с миксином
    advanced = AdvancedSubdir("Advanced Message", 42)
    advanced.show_message()
    advanced.extra_message()
    advanced.show_advanced()
    print()

    # Эксперт с глубокой цепочкой
    expert = ExpertSubdir("Expert Message", 99, 3.14)
    expert.show_message()
    expert.extra_message()
    expert.show_advanced()
    expert.show_expert()
    print()

    # Класс из subpy, который наследует ExpertSubdir
    subpy_obj = SubPyClass("SubPy Message", 7, 2.71)
    subpy_obj.show_message()
    subpy_obj.show_advanced()
    subpy_obj.show_expert()
    subpy_obj.subpy_method()
    print()
    with open("subpy_function.txt", "w") as f:
        f.write("Функция из subpy.py выполнена!")

    # Декоратор
    subpy_function()

if __name__ == "__main__":
    main()
