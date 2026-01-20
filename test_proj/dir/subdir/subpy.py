from functools import wraps
from dir.subdirr import ExpertSubdir

# Декоратор
def announce(func):
    @wraps(func)
    def wrapper(*args, **kwargs):
        print(f">>> Выполняется функция {func.__name__}...")
        result = func(*args, **kwargs)
        print(f">>> Функция {func.__name__} завершена")
        return result
    return wrapper

@announce
def subpy_function():
    print("Функция из subpy.py выполнена!")

# Класс, наследующий самый сложный класс из subdir.py
class SubPyClass(ExpertSubdir):
    def __init__(self, message, value, expert_value):
        super().__init__(message, value, expert_value)

    def subpy_method(self):
        print(f"SubPyClass: уникальный метод subpy (value={self.value}, expert={self.expert_value})")
