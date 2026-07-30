import random

# Компьютер загадывает число от 1 до 52
secret = random.randint(1, 52)
attempts = 0

print("Я загадал число от 1 до 52. Попробуй угадать!")

# Этот цикл повторяется, пока игрок не угадает
while True:
    try:
        guess = int(input("Твой вариант: "))
    except ValueError:
        print("Это не число! Попробуй ещё раз.")
        continue

    attempts = attempts + 1

    if guess < secret:
        print("Больше!")
    elif guess > secret:
        print("Меньше!")
    else:
        print(f"Мужчина! Число было {secret}. Попыток: {attempts}")
        break
