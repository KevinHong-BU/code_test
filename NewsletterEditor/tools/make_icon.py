"""res/app.ico(펼친 책 모양 아이콘)를 다시 만드는 스크립트. 사용법: python tools/make_icon.py"""
import os
import sys

from PIL import Image, ImageDraw

GREEN = (31, 111, 80, 255)
ACCENT = (242, 169, 59, 255)
PAPER = (255, 255, 255, 255)
LINE = (180, 200, 190, 255)


def draw(size: int) -> Image.Image:
    s = 256
    img = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    d.rounded_rectangle([8, 8, s - 8, s - 8], radius=48, fill=GREEN)
    # 펼친 책의 양쪽 면
    d.polygon([(40, 78), (124, 92), (124, 206), (40, 192)], fill=PAPER)
    d.polygon([(132, 92), (216, 78), (216, 192), (132, 206)], fill=PAPER)
    d.rectangle([124, 92, 132, 206], fill=ACCENT)
    # 본문 줄
    for i in range(4):
        y = 112 + i * 20
        d.line([(56, y), (110, y + 7)], fill=LINE, width=6)
        d.line([(146, y + 7), (200, y)], fill=LINE, width=6)
    # 책갈피
    d.polygon([(176, 60), (196, 60), (196, 104), (186, 94), (176, 104)], fill=ACCENT)
    return img.resize((size, size), Image.LANCZOS)


def main() -> None:
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    out = os.path.join(root, "res", "app.ico")
    sizes = [16, 24, 32, 48, 64, 256]
    big = draw(256)
    big.save(out, format="ICO", sizes=[(n, n) for n in sizes])
    print("wrote", out, file=sys.stderr)


if __name__ == "__main__":
    main()
