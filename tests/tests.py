import requests

BASE_URL = "http://localhost:9002/"

data1 = {"name": "Giga", "surname": "Chad", "age": 29}
data2 = {"name": "John", "surname": "Smith", "age": 33}

r = requests.get(url=f"{BASE_URL}users")
print(r.json())

r = requests.post(url=f"{BASE_URL}users", data=data1)
print(r.json())

r = requests.get(url=f"{BASE_URL}users")
print(r.json())

r = requests.get(url=f"{BASE_URL}users/1")
print(r.json())

r = requests.get(url=f"{BASE_URL}users/2")
print(r.json())

r = requests.get(url=f"{BASE_URL}users/234234")
print(r.json())

r = requests.delete(url=f"{BASE_URL}users/1")
print(r.json())

r = requests.get(url=f"{BASE_URL}users")
print(r.json())

r = requests.put(url=f"{BASE_URL}users/2", data=data2)
print(r.json())

r = requests.get(url=f"{BASE_URL}users")
print(r.json())
