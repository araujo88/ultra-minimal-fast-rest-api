import requests
from faker import Faker
import random

fake = Faker()

BASE_URL = "http://localhost:9002/"

for i in range(0, 20):
    name = fake.name()
    name = name.split(" ")
    data = {"name": name[0], "surname": name[1], "age": random.randint(
        18, 90), "height": round(random.uniform(1.50, 2), 2)}
    r = requests.post(url=f"{BASE_URL}users", data=data)
    print(r.json())

r = requests.get(url=f"{BASE_URL}users")
print(r.json())
