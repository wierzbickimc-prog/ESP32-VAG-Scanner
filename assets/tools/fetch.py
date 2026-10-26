"""Download the chosen Wikimedia Commons photos and write assets/CREDITS.md.

Run with a Python that has Pillow:  python fetch.py
"""
import html, json, os, re, urllib.parse, urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "source")
UA = {"User-Agent": "esp32-vag-scanner/1.0 (https://github.com/wierzbickimc-prog/ESP32-VAG-Scanner)"}

# name -> Commons file title
PHOTOS = {
    "eurovan": "File:2000 Volkswagen Eurovan MV, Silver, front left.jpg",
    "harlequin": "File:VWGolfHarlequin.JPG",
    "golf1": "File:VW Golf I front 20080208.jpg",
    "golf2": "File:VW Golf II front 20080102.jpg",
    "golf3": "File:VW Golf III front 20070522.jpg",
    "golf4": "File:VW Golf IV front 20071205.jpg",
    "golf5": "File:VW Golf V front 20071026.jpg",
    "golf6": "File:VW Golf VI front-1.JPG",
    "golf7": "File:Volkswagen Golf VII GTI front.JPG",
    "golf8": "File:Golf 8.jpg",
}


def get(url):
    return urllib.request.urlopen(urllib.request.Request(url, headers=UA), timeout=60).read()


def info(title):
    q = dict(action="query", format="json", prop="imageinfo", iiprop="url|extmetadata",
             iiurlwidth=2400, titles=title)
    page = list(json.loads(get("https://commons.wikimedia.org/w/api.php?" + urllib.parse.urlencode(q)))
                ["query"]["pages"].values())[0]
    return page["imageinfo"][0]


def text(meta, key):
    raw = meta.get(key, {}).get("value", "")
    return html.unescape(re.sub(r"<[^>]+>", "", raw)).strip()


def main():
    os.makedirs(SRC, exist_ok=True)
    lines = ["# Image credits", "",
             "Photos from Wikimedia Commons, used under their licences. Cropped, resized,",
             "and (golf8) mirrored for this app.", "",
             "| Image | Author | Licence | Source |", "|---|---|---|---|"]
    for name, title in PHOTOS.items():
        ii = info(title)
        path = os.path.join(SRC, name + ".jpg")
        if not os.path.exists(path):
            open(path, "wb").write(get(ii["thumburl"]))
        m = ii["extmetadata"]
        lines.append(f"| {name} | {text(m, 'Artist') or 'unknown'} | {text(m, 'LicenseShortName')} | "
                     f"[{title[5:]}]({ii['descriptionurl']}) |")
        print(name, "ok")
    open(os.path.join(HERE, "..", "CREDITS.md"), "w").write("\n".join(lines) + "\n")


if __name__ == "__main__":
    main()
