from abc import ABC, abstractmethod

# Абстрактный базовый класс
class BaseClass(ABC):
    @abstractmethod
    def show_message(self):
        pass

# Простое наследование
class SubdirClass(BaseClass):
    def __init__(self, message):
        self.message = message

    def show_message(self):
        print(f"SubdirClass: {self.message}")

# Наследник с доп. методом
class ChildSubdirClass(SubdirClass):
    def extra_message(self):
        print(f"ChildSubdirClass: доп. сообщение: {self.message.upper()}")

# Миксин для расширенных возможностей
class AdvancedMixin:
    def show_advanced(self):
        print(f"AdvancedMixin: значение * 2 = {self.value * 2}")

# Сложный класс с множественным наследованием
class AdvancedSubdir(ChildSubdirClass, AdvancedMixin):
    def __init__(self, message, value):
        super().__init__(message)
        self.value = value

    # Переопределение
    def show_message(self):
        print(f"AdvancedSubdir: {self.message} (value={self.value})")

# Еще более сложный класс, добавляем новый миксин и атрибут
class ExpertMixin:
    def show_expert(self):
        print(f"ExpertMixin: экспертное значение = {self.expert_value}")

class ExpertSubdir(AdvancedSubdir, ExpertMixin):
    def __init__(self, message, value, expert_value):
        super().__init__(message, value)
        self.expert_value = expert_value

    def show_message(self):
        print(f"ExpertSubdir: {self.message} (value={self.value}, expert={self.expert_value})")
