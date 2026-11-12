import json
import urllib.request

d = json.load(urllib.request.urlopen("http://127.0.0.1:9010/api/runtime/services"))["data"]
print("服务:", d["services"], " 方法:", d["methods"])
for k, v in d["detail"].items():
    print("  %-10s -> %s" % (k, ",".join(v)))
