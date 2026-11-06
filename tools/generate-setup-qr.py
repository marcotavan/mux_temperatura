"""Generate a printable Wi-Fi/setup label. Requires qrcode and Pillow.

Example: python tools/generate-setup-qr.py --mac cc:8d:a2:fb:75:d8
The password is read from the C6 sdkconfig, so the label matches the build.
"""
import argparse
import json
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
# Optional workspace-local dependencies; no global installation is required.
sys.path.insert(0, str(ROOT / "build-qr-deps"))
import qrcode
from qrcode.image.svg import SvgPathImage
from PIL import Image, ImageDraw, ImageFont


def wifi_escape(value):
    return re.sub(r'([\\;,:\"])', r'\\\1', value)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mac", required=True, help="Station/base MAC, e.g. cc:8d:a2:fb:75:d8")
    parser.add_argument("--sdkconfig", type=pathlib.Path, default=ROOT / "sdkconfig.esp32c6")
    parser.add_argument("--output", type=pathlib.Path, default=ROOT / "docs" / "etichette")
    args = parser.parse_args()
    mac = args.mac.replace(":", "").replace("-", "").upper()
    if not re.fullmatch(r"[0-9A-F]{12}", mac):
        parser.error("Usare il MAC base Wi-Fi a 6 byte")
    match = re.search(r'^CONFIG_CT_SETUP_AP_PASSWORD=(".*")$', args.sdkconfig.read_text(encoding="utf-8"), re.M)
    if not match:
        parser.error("Password AP assente: configurare/compilare prima il firmware")
    password = json.loads(match.group(1))
    ssid = "Centrale-Setup-" + mac[-6:]
    payload = f"WIFI:T:WPA;S:{wifi_escape(ssid)};P:{wifi_escape(password)};;"
    url = "http://192.168.4.1"
    args.output.mkdir(parents=True, exist_ok=True)
    stem = args.output / ("centrale-" + mac[-6:])

    wifi_qr = qrcode.QRCode(error_correction=qrcode.constants.ERROR_CORRECT_M, box_size=12, border=4)
    wifi_qr.add_data(payload)
    wifi_qr.make(fit=True)
    wifi_qr.make_image(image_factory=SvgPathImage).save(str(stem) + "-wifi.svg")
    wifi = wifi_qr.make_image().convert("RGB")
    wifi.save(str(stem) + "-wifi.png")
    page_qr = qrcode.QRCode(box_size=8, border=4)
    page_qr.add_data(url)
    page_qr.make(fit=True)
    page = page_qr.make_image().convert("RGB")

    label = Image.new("RGB", (900, 1320), "white")
    draw = ImageDraw.Draw(label)
    font_path = pathlib.Path("C:/Windows/Fonts/arial.ttf")

    def text(y, value, size=28):
        font = ImageFont.truetype(str(font_path), size) if font_path.exists() else ImageFont.load_default(size=size)
        draw.text((450, y), value, font=font, fill="black", anchor="mt")

    text(35, "CENTRALE TERMICA", 40)
    text(100, "1. Scansiona per collegarti al Wi-Fi", 30)
    label.paste(wifi, ((900 - wifi.width) // 2, 145))
    text(710, ssid, 34)
    text(760, "Password: " + password, 28)
    text(825, "2. Apri la pagina di configurazione", 30)
    label.paste(page, ((900 - page.width) // 2, 875))
    text(1150, url, 32)
    text(1210, "AP: pulsante 5 s e rilascio. Reset: 15 s e rilascio.", 24)
    text(1255, "La password AP resta uguale anche dopo il reset.", 24)
    label.save(str(stem) + "-etichetta.png", dpi=(300, 300))
    print("Etichetta:", str(stem) + "-etichetta.png")
    print("QR Wi-Fi:", str(stem) + "-wifi.svg")

    # Optional independent decoder verifies both QR codes in the final label.
    try:
        import zxingcpp
    except ImportError:
        return
    decoded = {item.text for item in zxingcpp.read_barcodes(label)}
    if decoded != {payload, url}:
        raise RuntimeError("Verifica QR etichetta fallita")
    print("Verifica indipendente: entrambi i QR decodificati correttamente")


if __name__ == "__main__":
    main()
