from multiprocessing import Pool
import requests
p = Pool(5)
print p.map(requests.get, ['https://localhost:7310'] * 5)
