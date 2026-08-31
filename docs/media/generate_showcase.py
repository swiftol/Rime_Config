"""Generate privacy-safe project images without capturing the desktop.

The output contains only synthetic public demo text and carries no user data.
Run with the bundled/system Python that provides Pillow.
"""

from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


ROOT = Path(__file__).resolve().parent
FONT = Path(r"C:\Windows\Fonts\msyh.ttc")
JP_FONT = Path(r"C:\Windows\Fonts\YuGothM.ttc")

BG = "#171b20"
PANEL = "#3d4147"
SELECTED = "#24bcc4"
TEXT = "#f4f6f8"
MUTED = "#c7ccd2"
JP = "#f6df3a"
ACCENT = "#19c7a3"


def font(size: int, japanese: bool = False) -> ImageFont.FreeTypeFont:
    return ImageFont.truetype(str(JP_FONT if japanese else FONT), size=size)


def rounded(draw: ImageDraw.ImageDraw, box, radius=18, fill=PANEL, outline=None, width=1):
    draw.rounded_rectangle(box, radius=radius, fill=fill, outline=outline, width=width)


def draw_candidate_window(draw: ImageDraw.ImageDraw, code: str, candidates: list[dict]):
    x0, y0, x1, y1 = 54, 112, 1146, 380
    rounded(draw, (x0, y0, x1, y1), 22, PANEL)
    draw.text((76, 132), code, font=font(30), fill="#38d6df")
    draw.polygon([(76 + max(28, len(code) * 16), 171), (84 + max(28, len(code) * 16), 171),
                  (80 + max(28, len(code) * 16), 165)], fill="#ff9c5b")

    widths = [250, 236, 236, 236]
    x = x0
    for index, item in enumerate(candidates):
        w = widths[index] if index < len(widths) else 220
        if index == 0:
            draw.rectangle((x, 190, x + w, y1), fill=SELECTED)
        pad = 18
        draw.text((x + pad, 207), f"{index + 1}  {item['text']}",
                  font=font(31, item.get("jp", False)), fill=JP if item.get("jp") else TEXT)
        if item.get("en"):
            draw.text((x + pad, 257), item["en"], font=font(22), fill=TEXT)
        if item.get("note"):
            draw.text((x + pad, 297), item["note"], font=font(22, True), fill=TEXT)
        x += w


def demo_frame(code: str, candidates: list[dict], caption: str) -> Image.Image:
    image = Image.new("RGB", (1200, 520), BG)
    draw = ImageDraw.Draw(image)
    draw.text((54, 34), "Rime 中日直输", font=font(35), fill=TEXT)
    draw.text((330, 43), "中文拼音 + 日本語ローマ字・无需前缀", font=font(22, True), fill=MUTED)
    draw_candidate_window(draw, code, candidates)
    draw.text((54, 426), caption, font=font(25), fill=TEXT)
    draw.text((54, 470), "隐私安全合成演示 · 不含桌面、账号或个人输入记录",
              font=font(18), fill="#8d969f")
    return image


def generate_gif() -> None:
    scenes = [
        (
            "nihao",
            [
                {"text": "你好", "en": "hello", "note": "こんにちは"},
                {"text": "拟好", "en": "draft well", "note": "うまく整える"},
                {"text": "你号", "en": "your number", "note": "あなたの番号"},
            ],
            "中文拼音直接输入，并显示英文与日文注释",
        ),
        (
            "nihongo",
            [
                {"text": "日本語", "note": "[にほんご]", "jp": True},
                {"text": "にほんご", "note": "[にほんご]", "jp": True},
                {"text": "ニホンゴ", "note": "[にほんご]", "jp": True},
            ],
            "日语罗马字无需切换模式或添加前缀",
        ),
        (
            "koqhiq",
            [
                {"text": "コーヒー", "note": "[こーひー]", "jp": True},
                {"text": "珈琲", "note": "[こーひー]", "jp": True},
                {"text": "咖啡", "en": "coffee", "note": "コーヒー"},
            ],
            "支持长音、促音、浊音和可配置的日语模糊匹配",
        ),
        (
            "huiyi",
            [
                {"text": "会议", "en": "meeting", "note": "会議"},
                {"text": "会議", "note": "[かいぎ]", "jp": True},
                {"text": "回忆", "en": "memory", "note": "思い出"},
            ],
            "同一候选窗中识别中文候选与自然日语候选",
        ),
    ]
    frames = [demo_frame(*scene) for scene in scenes]
    frames[0].save(
        ROOT / "mixed-input-demo.gif",
        save_all=True,
        append_images=frames[1:],
        duration=[1500, 1500, 1700, 1700],
        loop=0,
        optimize=True,
        disposal=2,
    )


def generate_settings_preview() -> None:
    image = Image.new("RGB", (1200, 650), BG)
    draw = ImageDraw.Draw(image)
    rounded(draw, (34, 28, 1166, 622), 24, "#1f242a", "#343a42")
    draw.text((72, 62), "中日方案设置", font=font(38), fill=TEXT)
    draw.text((72, 113), "集中管理候选注释、输入行为、模糊匹配与外观",
              font=font(21), fill=MUTED)

    tabs = ["输入", "中文模糊纠错", "日语模糊匹配", "外观", "维护"]
    y = 174
    for i, tab in enumerate(tabs):
        fill = "#148873" if i == 0 else "#252b31"
        rounded(draw, (64, y, 285, y + 58), 12, fill)
        draw.text((91, y + 13), tab, font=font(22), fill=TEXT)
        y += 72

    rows = [
        ("候选注释", "英文与日文注释可分别开关", True),
        ("空格行为", "原文上屏 / 选择首选 / 按住预览读音", True),
        ("Alt 读音预览", "按住显示日语读音，松开恢复", True),
        ("单字候选注释", "可隐藏过长的单字翻译注释", False),
        ("展开候选布局", "逐行滚动、动态宽度与注释对齐", True),
    ]
    y = 174
    for title, desc, enabled in rows:
        rounded(draw, (330, y, 1128, y + 76), 13, "#282e34")
        draw.text((358, y + 10), title, font=font(23), fill=TEXT)
        draw.text((358, y + 43), desc, font=font(17), fill="#aeb6be")
        tx = 1030
        rounded(draw, (tx, y + 22, tx + 66, y + 52), 15, ACCENT if enabled else "#59616a")
        knob_x = tx + 50 if enabled else tx + 16
        draw.ellipse((knob_x - 11, y + 26, knob_x + 11, y + 48), fill="#ffffff")
        y += 88

    draw.text((330, 588), "所有设置保存在本机，不需要登录账号",
              font=font(18), fill="#8d969f")
    image.save(ROOT / "settings-preview.png", optimize=True)


if __name__ == "__main__":
    ROOT.mkdir(parents=True, exist_ok=True)
    generate_gif()
    generate_settings_preview()
