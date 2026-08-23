#!/usr/bin/env python3
"""Generate the Chinese Prism Viewer 1.0.0 user operation manual."""

from __future__ import annotations

import os
import hashlib
from pathlib import Path

from reportlab.lib import colors
from reportlab.lib.enums import TA_CENTER, TA_JUSTIFY, TA_LEFT
from reportlab.lib.pagesizes import A4
from reportlab.lib.styles import ParagraphStyle, getSampleStyleSheet
from reportlab.lib.units import mm
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.platypus import (
    BaseDocTemplate,
    CondPageBreak,
    Flowable,
    Image,
    KeepTogether,
    ListFlowable,
    ListItem,
    NextPageTemplate,
    PageBreak,
    PageTemplate,
    Paragraph,
    Spacer,
    Table,
    TableStyle,
    Frame,
)
from reportlab.platypus.tableofcontents import TableOfContents


SCRIPT_DIR = Path(__file__).resolve().parent
REPO_ROOT = SCRIPT_DIR.parents[1]
OUT = REPO_ROOT / "docs/Prism-Viewer-1.0.0-用户操作手册.pdf"
LOGO = REPO_ROOT / "branding/prism-mark-256.png"
SCREENSHOT_DIR = SCRIPT_DIR / "assets/screenshots"

PAGE_W, PAGE_H = A4
MARGIN_X = 17 * mm
MARGIN_TOP = 19 * mm
MARGIN_BOTTOM = 17 * mm

NAVY = colors.HexColor("#101828")
TEXT = colors.HexColor("#24364D")
MUTED = colors.HexColor("#667085")
LINE = colors.HexColor("#D9E2EF")
PALE = colors.HexColor("#F4F7FB")
BLUE = colors.HexColor("#175CD3")
PALE_BLUE = colors.HexColor("#EFF8FF")
GREEN = colors.HexColor("#027A48")
PALE_GREEN = colors.HexColor("#ECFDF3")
ORANGE = colors.HexColor("#B54708")
PALE_ORANGE = colors.HexColor("#FFFAEB")
RED = colors.HexColor("#B42318")
PALE_RED = colors.HexColor("#FEF3F2")
TEAL = colors.HexColor("#0E9384")


def resolve_font(environment_name: str, candidates: tuple[str, ...]) -> str:
    configured = os.environ.get(environment_name)
    if configured:
        path = Path(configured).expanduser()
        if not path.is_file():
            raise FileNotFoundError(
                f"{environment_name} does not point to a font file: {path}"
            )
        return str(path)
    for candidate in candidates:
        if Path(candidate).is_file():
            return candidate
    raise FileNotFoundError(
        f"no CJK font found; set {environment_name} to a usable TTF/TTC file"
    )


REGULAR_FONT = resolve_font(
    "PRISM_MANUAL_CJK_FONT",
    (
        "/System/Library/Fonts/Supplemental/Arial Unicode.ttf",
        "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/truetype/noto/NotoSansCJK-Regular.ttc",
    ),
)
BOLD_FONT = resolve_font(
    "PRISM_MANUAL_CJK_BOLD_FONT",
    (
        "/System/Library/Fonts/STHeiti Medium.ttc",
        "/usr/share/fonts/opentype/noto/NotoSansCJK-Bold.ttc",
        "/usr/share/fonts/truetype/noto/NotoSansCJK-Bold.ttc",
    ),
)

pdfmetrics.registerFont(TTFont("CJK", REGULAR_FONT))
pdfmetrics.registerFont(TTFont("CJK-Bold", BOLD_FONT))


def esc(text: str) -> str:
    return (
        text.replace("&", "&amp;")
        .replace("<", "&lt;")
        .replace(">", "&gt;")
    )


styles = getSampleStyleSheet()
styles.add(ParagraphStyle(
    name="BodyCN", fontName="CJK", fontSize=9.2, leading=14.2,
    textColor=TEXT, alignment=TA_JUSTIFY, spaceAfter=3 * mm,
    wordWrap="CJK",
))
styles.add(ParagraphStyle(
    name="SmallCN", parent=styles["BodyCN"], fontSize=7.8, leading=11.5,
    textColor=MUTED, spaceAfter=1.5 * mm,
))
styles.add(ParagraphStyle(
    name="TinyCN", parent=styles["SmallCN"], fontSize=7.0, leading=9.5,
    spaceAfter=0,
))
styles.add(ParagraphStyle(
    name="TitleCN", fontName="CJK-Bold", fontSize=27, leading=36,
    textColor=colors.white, alignment=TA_LEFT, spaceAfter=4 * mm,
))
styles.add(ParagraphStyle(
    name="SubtitleCN", fontName="CJK", fontSize=12.2, leading=19,
    textColor=colors.HexColor("#D8E8FF"), spaceAfter=3 * mm,
))
styles.add(ParagraphStyle(
    name="H1CN", fontName="CJK-Bold", fontSize=18, leading=25,
    textColor=NAVY, spaceBefore=3 * mm, spaceAfter=4 * mm,
    keepWithNext=True,
))
styles.add(ParagraphStyle(
    name="H2CN", fontName="CJK-Bold", fontSize=12.5, leading=18,
    textColor=BLUE, spaceBefore=3.5 * mm, spaceAfter=2 * mm,
    keepWithNext=True,
))
styles.add(ParagraphStyle(
    name="H3CN", fontName="CJK-Bold", fontSize=10.2, leading=15,
    textColor=TEXT, spaceBefore=2.5 * mm, spaceAfter=1.5 * mm,
    keepWithNext=True,
))
styles.add(ParagraphStyle(
    name="StepTitle", fontName="CJK-Bold", fontSize=9.5, leading=14,
    textColor=NAVY, spaceAfter=1 * mm,
))
styles.add(ParagraphStyle(
    name="TableHead", fontName="CJK-Bold", fontSize=8.0, leading=11,
    textColor=colors.white, alignment=TA_LEFT, wordWrap="CJK",
))
styles.add(ParagraphStyle(
    name="TableCell", fontName="CJK", fontSize=7.6, leading=11,
    textColor=TEXT, wordWrap="CJK",
))
styles.add(ParagraphStyle(
    name="TableCellBold", parent=styles["TableCell"], fontName="CJK-Bold",
))
styles.add(ParagraphStyle(
    name="FigureCaption", fontName="CJK", fontSize=7.5, leading=11,
    textColor=MUTED, alignment=TA_CENTER, spaceBefore=1.5 * mm,
    spaceAfter=3 * mm, wordWrap="CJK",
))
styles.add(ParagraphStyle(
    name="CalloutTitle", fontName="CJK-Bold", fontSize=9.5, leading=13,
    spaceAfter=1 * mm,
))
styles.add(ParagraphStyle(
    name="TOCHeading", fontName="CJK-Bold", fontSize=10.5, leading=16,
    leftIndent=0, firstLineIndent=0, textColor=TEXT,
))
styles.add(ParagraphStyle(
    name="TOCLevel1", fontName="CJK", fontSize=9.0, leading=15,
    leftIndent=0, firstLineIndent=0, textColor=TEXT,
))


class ManualDocTemplate(BaseDocTemplate):
    def __init__(self, filename: str):
        super().__init__(
            filename,
            pagesize=A4,
            leftMargin=MARGIN_X,
            rightMargin=MARGIN_X,
            topMargin=MARGIN_TOP,
            bottomMargin=MARGIN_BOTTOM,
            title="Prism Viewer 1.0.0 用户操作手册",
            author="DIBULI",
            subject="Prism Viewer 功能操作指南",
        )
        body_frame = Frame(
            MARGIN_X, MARGIN_BOTTOM,
            PAGE_W - 2 * MARGIN_X,
            PAGE_H - MARGIN_TOP - MARGIN_BOTTOM,
            id="body", topPadding=8 * mm, bottomPadding=5 * mm,
            leftPadding=0, rightPadding=0,
        )
        cover_frame = Frame(
            0, 0, PAGE_W, PAGE_H, id="cover",
            leftPadding=0, rightPadding=0, topPadding=0, bottomPadding=0,
        )
        self.addPageTemplates([
            PageTemplate(id="Cover", frames=[cover_frame], onPage=self.draw_cover_bg),
            PageTemplate(id="Body", frames=[body_frame], onPage=self.draw_body_chrome),
        ])

    def afterFlowable(self, flowable):
        if isinstance(flowable, Paragraph):
            style = flowable.style.name
            if style in ("H1CN", "H2CN"):
                level = 0 if style == "H1CN" else 1
                text = flowable.getPlainText()
                key = "heading-" + hashlib.sha1(text.encode("utf-8")).hexdigest()[:16]
                self.canv.bookmarkPage(key)
                self.canv.addOutlineEntry(text, key, level=level, closed=False)
                self.notify("TOCEntry", (level, text, self.page, key))

    def draw_cover_bg(self, canvas, doc):
        canvas.saveState()
        canvas.setFillColor(NAVY)
        canvas.rect(0, 0, PAGE_W, PAGE_H, fill=1, stroke=0)
        canvas.setFillColor(colors.HexColor("#173968"))
        canvas.circle(PAGE_W + 30 * mm, PAGE_H - 38 * mm, 88 * mm, fill=1, stroke=0)
        canvas.setFillColor(colors.HexColor("#0C6F65"))
        canvas.circle(-25 * mm, 18 * mm, 70 * mm, fill=1, stroke=0)
        canvas.setFillColor(colors.HexColor("#214C80"))
        canvas.roundRect(18 * mm, 17 * mm, PAGE_W - 36 * mm, 29 * mm,
                         5 * mm, fill=1, stroke=0)
        canvas.restoreState()

    def draw_body_chrome(self, canvas, doc):
        canvas.saveState()
        canvas.setStrokeColor(LINE)
        canvas.setLineWidth(0.5)
        canvas.line(MARGIN_X, PAGE_H - 13 * mm, PAGE_W - MARGIN_X, PAGE_H - 13 * mm)
        if LOGO.exists():
            canvas.drawImage(str(LOGO), MARGIN_X, PAGE_H - 11.8 * mm,
                             6.2 * mm, 6.2 * mm, mask="auto", preserveAspectRatio=True)
        canvas.setFont("CJK-Bold", 7.5)
        canvas.setFillColor(MUTED)
        canvas.drawString(MARGIN_X + 8 * mm, PAGE_H - 9.6 * mm,
                          "PRISM VIEWER 1.0.0 · 用户操作手册")
        canvas.setFont("CJK", 7.2)
        canvas.drawRightString(PAGE_W - MARGIN_X, PAGE_H - 9.6 * mm,
                               f"第 {doc.page} 页")
        canvas.setStrokeColor(LINE)
        canvas.line(MARGIN_X, 12 * mm, PAGE_W - MARGIN_X, 12 * mm)
        canvas.setFillColor(MUTED)
        canvas.setFont("CJK", 6.8)
        canvas.drawString(MARGIN_X, 8.2 * mm, "DIBULI · 2026-08-23")
        canvas.drawRightString(PAGE_W - MARGIN_X, 8.2 * mm,
                               "请以设备状态栏和升级包版本提示为准")
        canvas.restoreState()


class CoverContent(Flowable):
    def __init__(self):
        super().__init__()
        self.width = PAGE_W
        self.height = PAGE_H

    def wrap(self, availWidth, availHeight):
        return self.width, self.height

    def draw(self):
        c = self.canv
        if LOGO.exists():
            c.drawImage(str(LOGO), 22 * mm, PAGE_H - 54 * mm,
                        25 * mm, 25 * mm, preserveAspectRatio=True, mask="auto")
        c.setFont("CJK-Bold", 8.5)
        c.setFillColor(colors.HexColor("#9FC5F8"))
        c.drawString(22 * mm, PAGE_H - 64 * mm, "DIBULI PRISM-A4L")
        c.setFillColor(colors.white)
        c.setFont("CJK-Bold", 28)
        c.drawString(22 * mm, PAGE_H - 87 * mm, "Prism Viewer")
        c.setFont("CJK-Bold", 24)
        c.drawString(22 * mm, PAGE_H - 103 * mm, "用户操作手册")
        c.setFillColor(colors.HexColor("#D8E8FF"))
        c.setFont("CJK", 12)
        c.drawString(22 * mm, PAGE_H - 119 * mm,
                     "设备连接 · 时间同步 · Camera · IMU · LiDAR · 数据集 · 系统升级")

        c.setFillColor(colors.HexColor("#214C80"))
        c.roundRect(22 * mm, PAGE_H - 163 * mm, 101 * mm, 27 * mm,
                    4 * mm, fill=1, stroke=0)
        c.setFillColor(colors.white)
        c.setFont("CJK-Bold", 9)
        c.drawString(27 * mm, PAGE_H - 146 * mm, "文档版本")
        c.setFont("CJK", 9)
        c.drawString(27 * mm, PAGE_H - 153.5 * mm, "Viewer 1.0.0 / USB 协议 1")
        c.drawString(27 * mm, PAGE_H - 160 * mm, "适用主机：macOS arm64、Windows x64、Linux x64")

        c.setFillColor(colors.white)
        c.setFont("CJK-Bold", 9.2)
        c.drawString(23 * mm, 35 * mm, "验证组合")
        c.setFont("CJK", 8.4)
        c.drawString(56 * mm, 35 * mm,
                     "Agent 1.0.0 · Host SDK 1.0.0 · sensor-board 0.4.16")
        c.setFont("CJK", 7.5)
        c.setFillColor(colors.HexColor("#C7D7ED"))
        c.drawString(23 * mm, 26 * mm,
                     "本手册面向操作人员；所有功能名称与 Viewer 中文界面一致。")


class UiMapFlowable(Flowable):
    def __init__(self):
        super().__init__()
        self.width = 176 * mm
        self.height = 86 * mm

    def wrap(self, availWidth, availHeight):
        return min(self.width, availWidth), self.height

    def draw_box(self, x, y, w, h, label, fill, stroke=LINE, size=7.3):
        c = self.canv
        c.setFillColor(fill)
        c.setStrokeColor(stroke)
        c.roundRect(x, y, w, h, 2 * mm, fill=1, stroke=1)
        c.setFillColor(TEXT)
        c.setFont("CJK-Bold", size)
        c.drawCentredString(x + w / 2, y + h / 2 - 2.2, label)

    def draw(self):
        c = self.canv
        w = self.width
        c.setFillColor(colors.white)
        c.setStrokeColor(LINE)
        c.roundRect(0, 0, w, self.height, 4 * mm, fill=1, stroke=1)
        c.setFillColor(PALE)
        c.roundRect(5 * mm, 65 * mm, w - 10 * mm, 15 * mm, 3 * mm, fill=1, stroke=0)
        self.draw_box(8 * mm, 69 * mm, 38 * mm, 7 * mm, "设备选择 / 打开 / 关闭", colors.white)
        self.draw_box(49 * mm, 69 * mm, 32 * mm, 7 * mm, "开始采集 / 停止", PALE_GREEN)
        self.draw_box(84 * mm, 69 * mm, 31 * mm, 7 * mm, "录制 / 停止录制", PALE_BLUE)
        self.draw_box(118 * mm, 69 * mm, 48 * mm, 7 * mm, "校准时间 / 升级 / 日志 / 语言", PALE_ORANGE, size=6.7)
        self.draw_box(8 * mm, 56 * mm, 50 * mm, 6.5 * mm, "设备状态", colors.white)
        self.draw_box(62 * mm, 56 * mm, 48 * mm, 6.5 * mm, "传感器时间同步", colors.white)
        self.draw_box(114 * mm, 56 * mm, 52 * mm, 6.5 * mm, "主机 / 设备时间偏差", colors.white)
        tabs = ["设备信息", "相机", "IMU", "LiDAR", "网络", "本地数据集"]
        x = 8 * mm
        for i, tab in enumerate(tabs):
            tw = 25 * mm if i != 5 else 33 * mm
            self.draw_box(x, 45 * mm, tw, 7 * mm, tab,
                          PALE_BLUE if i == 1 else PALE)
            x += tw + 2 * mm
        c.setFillColor(colors.HexColor("#08111F"))
        c.roundRect(8 * mm, 7 * mm, 102 * mm, 33 * mm, 3 * mm, fill=1, stroke=0)
        c.setFillColor(colors.HexColor("#C5D4E8"))
        c.setFont("CJK-Bold", 8)
        c.drawString(13 * mm, 34 * mm, "当前功能主视图")
        c.setFont("CJK", 7)
        c.drawString(13 * mm, 26 * mm, "相机四画面 / IMU 曲线 / 点云 / 数据集画面")
        c.drawString(13 * mm, 18 * mm, "按所选主标签切换；画面区自动适配窗口")
        c.setFillColor(PALE)
        c.roundRect(115 * mm, 7 * mm, 51 * mm, 33 * mm, 3 * mm, fill=1, stroke=0)
        c.setFillColor(TEXT)
        c.setFont("CJK-Bold", 8)
        c.drawString(120 * mm, 34 * mm, "侧边工具区")
        c.setFont("CJK", 7)
        c.drawString(120 * mm, 26 * mm, "Camera：相机流 / 曝光 / 元数据")
        c.drawString(120 * mm, 18 * mm, "其他标签：对应状态与操作按钮")


def P(text: str, style: str = "BodyCN") -> Paragraph:
    return Paragraph(text, styles[style])


def H1(text: str) -> Paragraph:
    return Paragraph(text, styles["H1CN"])


def H2(text: str) -> Paragraph:
    return Paragraph(text, styles["H2CN"])


def H3(text: str) -> Paragraph:
    return Paragraph(text, styles["H3CN"])


def bullets(items, level=0):
    return ListFlowable(
        [ListItem(P(item, "BodyCN"), leftIndent=5 * mm) for item in items],
        bulletType="bullet", bulletFontName="CJK", bulletFontSize=6.5,
        leftIndent=(5 + level * 5) * mm, bulletOffsetY=1.5,
        spaceAfter=2 * mm,
    )


def numsteps(items):
    rows = []
    for idx, (title, body) in enumerate(items, 1):
        badge = Table([[P(str(idx), "TableCellBold")]], colWidths=[8 * mm], rowHeights=[8 * mm])
        badge.setStyle(TableStyle([
            ("BACKGROUND", (0, 0), (-1, -1), BLUE),
            ("TEXTCOLOR", (0, 0), (-1, -1), colors.white),
            ("ALIGN", (0, 0), (-1, -1), "CENTER"),
            ("VALIGN", (0, 0), (-1, -1), "MIDDLE"),
            ("BOX", (0, 0), (-1, -1), 0, BLUE),
        ]))
        text = [P(title, "StepTitle"), P(body, "SmallCN")]
        rows.append([badge, text])
    t = Table(rows, colWidths=[12 * mm, 154 * mm], hAlign="LEFT")
    t.setStyle(TableStyle([
        ("VALIGN", (0, 0), (-1, -1), "TOP"),
        ("LEFTPADDING", (0, 0), (-1, -1), 0),
        ("RIGHTPADDING", (0, 0), (-1, -1), 4 * mm),
        ("TOPPADDING", (0, 0), (-1, -1), 1.5 * mm),
        ("BOTTOMPADDING", (0, 0), (-1, -1), 2.5 * mm),
    ]))
    return t


def callout(title, body, kind="info"):
    palette = {
        "info": (PALE_BLUE, BLUE),
        "ok": (PALE_GREEN, GREEN),
        "warn": (PALE_ORANGE, ORANGE),
        "danger": (PALE_RED, RED),
    }
    bg, accent = palette[kind]
    data = [[P(title, "CalloutTitle")], [P(body, "SmallCN")]]
    t = Table(data, colWidths=[164 * mm], hAlign="LEFT")
    t.setStyle(TableStyle([
        ("BACKGROUND", (0, 0), (-1, -1), bg),
        ("TEXTCOLOR", (0, 0), (-1, 0), accent),
        ("LINEBEFORE", (0, 0), (0, -1), 3, accent),
        ("BOX", (0, 0), (-1, -1), 0.5, colors.Color(accent.red, accent.green, accent.blue, alpha=0.25)),
        ("LEFTPADDING", (0, 0), (-1, -1), 4 * mm),
        ("RIGHTPADDING", (0, 0), (-1, -1), 4 * mm),
        ("TOPPADDING", (0, 0), (-1, 0), 3 * mm),
        ("BOTTOMPADDING", (0, 0), (-1, 0), 0),
        ("TOPPADDING", (0, 1), (-1, 1), 0),
        ("BOTTOMPADDING", (0, 1), (-1, 1), 3 * mm),
    ]))
    return KeepTogether([t, Spacer(1, 3 * mm)])


def table(headers, rows, widths, small=False):
    style_name = "TinyCN" if small else "TableCell"
    data = [[P(h, "TableHead") for h in headers]]
    for row in rows:
        data.append([P(str(v), style_name) for v in row])
    t = Table(data, colWidths=widths, repeatRows=1, hAlign="LEFT", splitByRow=1)
    t.setStyle(TableStyle([
        ("BACKGROUND", (0, 0), (-1, 0), BLUE),
        ("TEXTCOLOR", (0, 0), (-1, 0), colors.white),
        ("GRID", (0, 0), (-1, -1), 0.4, LINE),
        ("ROWBACKGROUNDS", (0, 1), (-1, -1), [colors.white, PALE]),
        ("VALIGN", (0, 0), (-1, -1), "TOP"),
        ("LEFTPADDING", (0, 0), (-1, -1), 2.2 * mm),
        ("RIGHTPADDING", (0, 0), (-1, -1), 2.2 * mm),
        ("TOPPADDING", (0, 0), (-1, -1), 2 * mm),
        ("BOTTOMPADDING", (0, 0), (-1, -1), 2 * mm),
    ]))
    return t


def screenshot_figure(filename: str, caption: str, max_width=164 * mm,
                      max_height=102 * mm):
    path = SCREENSHOT_DIR / filename
    if not path.exists():
        raise FileNotFoundError(f"missing Viewer screenshot: {path}")
    image = Image(str(path))
    image._restrictSize(max_width, max_height)
    framed = Table([[image]], colWidths=[image.drawWidth], hAlign="CENTER")
    framed.setStyle(TableStyle([
        ("BOX", (0, 0), (-1, -1), 0.6, LINE),
        ("BACKGROUND", (0, 0), (-1, -1), colors.white),
        ("LEFTPADDING", (0, 0), (-1, -1), 1.2 * mm),
        ("RIGHTPADDING", (0, 0), (-1, -1), 1.2 * mm),
        ("TOPPADDING", (0, 0), (-1, -1), 1.2 * mm),
        ("BOTTOMPADDING", (0, 0), (-1, -1), 1.2 * mm),
    ]))
    return KeepTogether([
        framed,
        Paragraph(esc(caption), styles["FigureCaption"]),
    ])


def section_break(story):
    story.extend([Spacer(1, 2 * mm)])


def build_story():
    story = [CoverContent(), NextPageTemplate("Body"), PageBreak()]

    story += [H1("使用范围与安全说明")]
    story += [P(
        "本手册说明 Prism Viewer 1.0.0 的日常操作，包括 USB 设备连接、时间同步、四路相机预览、"
        "运行时曝光与增益、板载 IMU、Livox 点云、雷达网络、Wi-Fi 热点、数据集录制与回放、"
        "ROS Bag 导出和系统升级。界面按钮名称以中文模式为准，英文模式位置与功能相同。"
    )]
    story += [callout(
        "截图说明",
        "文中的真实界面截图来自 Viewer 1.0.0 英文模式测试设备；中文模式的控件位置和操作顺序相同。截图中的序列号、SSID、地址和参数仅为示例，Camera 实拍区域已经打码。设备信息和 IMU 截图使用双 IMU 演示机；标准产品默认标配单 IMU。",
        "info",
    )]
    story += [callout(
        "安全原则",
        "系统升级、时间同步、相机流持久配置、雷达网络和 Wi-Fi 热点操作都要求先停止采集。升级期间保持稳定供电，禁止拔 USB、断电或关闭 Viewer。",
        "danger",
    )]
    story += [table(
        ["项目", "本文档采用的值", "说明"],
        [
            ["Viewer", "1.0.0", "主机端图形界面"],
            ["Agent / Host SDK", "1.0.0 / 1.0.0", "设备端服务与 Viewer 内置 SDK 应保持同版本"],
            ["USB 协议", "1", "连接时由 Viewer 与 Agent 校验"],
            ["sensor-board", "0.4.16（验证组合）", "实际安装版本可在“设备信息”中查看"],
            ["推荐链路", "USB 3.x SuperSpeed", "四相机、IMU 和 LiDAR 联合采集优先使用高速链路"],
        ], [32 * mm, 44 * mm, 88 * mm]
    )]
    story += [H2("操作状态的三个阶段")]
    story += [table(
        ["状态", "可以做什么", "不能做什么"],
        [
            ["设备已打开，未采集", "查看设备信息、校准时间、修改持久配置、雷达网络、Wi-Fi、升级", "录制按钮尚不可用"],
            ["采集中", "看相机/IMU/LiDAR、实时调曝光、开始或停止录制", "时间同步、持久配置、网络和升级"],
            ["录制中", "继续查看画面、停止录制", "关闭设备、导出 Bag、验证当前录制目录"],
        ], [34 * mm, 66 * mm, 64 * mm]
    )]
    story += [PageBreak(), H1("目录")]
    toc = TableOfContents()
    toc.levelStyles = [styles["TOCHeading"], styles["TOCLevel1"]]
    story += [toc, PageBreak()]

    story += [H1("1. 六步快速开始")]
    story += [numsteps([
        ("连接设备", "给 Prism-A4L 上电，使用可靠的 USB 3.x 数据线连接电脑。不要通过仅供电线缆连接。"),
        ("打开设备", "启动 Viewer。只有一台设备时 Viewer 会尝试自动打开；多台设备时选择序列号，再单击“打开设备”。"),
        ("确认健康状态", "在“设备信息”中确认 USB 3 已连接、sensor-board 在线、四路相机和设备实际配备的板载 IMU 已检测到、错误标志为 0。标准产品默认显示 IMU0。"),
        ("完成时间同步", "启动时 Viewer 会自动校准设备时间。若失败，确保采集已停止后单击“校准设备时间”。等待 sensor-board 与所有已检测到的板载 IMU 显示已同步。"),
        ("选择是否启用 LiDAR", "需要雷达时，在 LiDAR 标签勾选“采集时启用雷达”，并明确选择 Mid-360 或 Mid-360S。型号不能留空。"),
        ("开始采集", "单击“开始采集”。观察四路相机帧率、IMU 采样率和可选点云；需要保存时再从“录制...”菜单选择模式。"),
    ])]
    story += [callout(
        "开始采集前建议",
        "先在空闲状态设置相机 FPS 与 JPEG 质量，再开始采集。曝光和增益属于运行时设置，可在采集中边看画面边调整。",
        "ok",
    )]
    story += [H2("一次标准采集的结束顺序")]
    story += [P("若正在录制：先单击“停止录制”并等待数据集完成，再单击“停止”，最后才“关闭设备”。直接关闭应用可能得到 complete=0 的未完成数据集。")]

    story += [PageBreak(), H1("2. 主界面与状态提示")]
    story += [screenshot_figure(
        "01-main-device-info.jpeg",
        "图 2-1  Viewer 主界面与设备信息页：顶部为设备、采集、时间同步、升级和日志操作；中间状态条显示时间健康状态；下方标签页进入各功能模块。",
    )]
    story += [UiMapFlowable(), Spacer(1, 4 * mm)]
    story += [H2("顶部操作区")]
    story += [table(
        ["控件", "用途", "可用条件"],
        [
            ["设备 / 刷新 / 打开设备 / 关闭设备", "枚举 USB 设备并管理会话", "关闭设备前必须停止采集和录制"],
            ["开始采集 / 停止", "同时管理 Camera 与板载 IMU；可选 LiDAR", "开始前设备必须已打开"],
            ["录制... / 停止录制", "完整数据集或仅 IMU 数据集", "只有采集中才可开始录制"],
            ["校准设备时间", "以主机时间校准 RK、以太网 PHC 与 RTC", "所有数据流停止"],
            ["系统升级", "升级 Agent 和 sensor-board 固件", "设备空闲且电源稳定"],
            ["打开日志", "查看运行日志并复制排障信息", "任何状态"],
            ["中文 / English", "切换界面语言，Viewer 自动重启一次", "设备关闭且无操作进行"],
        ], [45 * mm, 72 * mm, 47 * mm]
    )]
    story += [H2("状态颜色")]
    story += [table(
        ["颜色", "含义", "处理"],
        [
            ["绿色", "操作成功或状态健康", "可以继续下一步"],
            ["蓝色", "正在执行读取、同步或应用", "等待操作完成"],
            ["黄色", "尚未测试、状态未知、配置已保存但未验证", "按提示刷新或执行测试"],
            ["红色", "操作失败或设备报告错误", "停止操作，打开日志，根据错误排查"],
            ["灰色", "尚未读取、设备关闭或控件不可用", "先满足对应前置条件"],
        ], [25 * mm, 76 * mm, 63 * mm]
    )]

    story += [PageBreak(), H1("3. 设备连接、打开与时间同步")]
    story += [H2("3.1 打开设备")]
    story += [numsteps([
        ("检查物理连接", "设备上电后再接 USB。优先直连电脑 USB 3 端口，避免无供电能力或共享带宽的集线器。"),
        ("刷新列表", "单击“刷新”。设备下拉框以 USB 序列号区分多台设备。"),
        ("打开会话", "选择目标设备并单击“打开设备”。打开成功后 Camera、IMU、LiDAR、网络标签变为可用。"),
        ("核对版本", "打开“设备信息”，单击“刷新版本”，确认 Agent 与 Viewer/SDK 组合匹配。"),
    ])]
    story += [H2("3.2 自动与手动时间同步")]
    story += [P(
        "当只检测到一台设备时，Viewer 会在启动后尝试自动打开并执行时间同步。同步使用主机时间作为基准，写入 RK CLOCK_REALTIME、以太网 PHC 和硬件 RTC，然后重新测量剩余偏差。"
    )]
    story += [bullets([
        "若顶部显示自动同步成功，继续等待“sensor-board 时间已同步”和所有已检测到的板载 IMU 已同步。",
        "若自动同步失败，停止采集后单击“校准设备时间”重试。",
        "时间同步失败不会自动禁止预览，但录制严格要求 sensor-board 和设备实际检测到的每个板载 IMU 都处于同步状态。",
        "同步主机与设备时钟不等于改变相机曝光时刻；相机数据时间戳仍来自公共 TRIG0 触发时刻。",
    ])]
    story += [callout(
        "为什么“校准设备时间”按钮是灰色？",
        "Camera、IMU 或 LiDAR 仍在传输，或者其他设备操作尚未结束。先单击“停止”，等待状态回到设备已打开，再重试。",
        "warn",
    )]
    story += [H2("3.3 设备信息检查清单")]
    story += [table(
        ["字段", "正常值", "异常时优先检查"],
        [
            ["USB 3 已连接", "是 / super-speed", "USB 线、接口、主机权限、是否被另一进程占用"],
            ["sensor-board 在线", "是", "供电、板间连接、固件状态"],
            ["时间已同步", "是", "时间同步、GPS/PPS/设备启动状态"],
            ["检测到的相机", "Camera 0-3", "sensor-board、FPC、传感器供电"],
            ["检测到的 IMU", "标准版：IMU0；双 IMU 版：IMU0-1", "应与实际产品配置一致；否则检查 sensor-board 固件和串行链路"],
            ["四路传输状态", "空闲时停止；采集时四路全部传输", "若只出现部分相机，应停止并查看日志"],
            ["传输错误", "- / 0", "复制错误码与日志后排查"],
        ], [43 * mm, 51 * mm, 70 * mm]
    )]

    story += [PageBreak(), H1("4. Camera 采集与画面查看")]
    story += [H2("4.1 开始与停止")]
    story += [numsteps([
        ("进入相机标签", "确认四个画面框都显示 Camera 0、1、2、3。"),
        ("开始采集", "单击顶部“开始采集”。Viewer 会等待 RK 与 sensor-board 链路就绪，再启动四相机和设备实际检测到的板载 IMU。"),
        ("确认完整帧组", "每路下方的“接收完整帧组”和 FPS 应持续增长。四路计数应基本一致。"),
        ("停止采集", "不再预览或准备修改持久配置时单击“停止”。停止后四路传输状态应回到 0。"),
    ])]
    story += [callout(
        "FPS 的正确理解",
        "Viewer 统计的是完成并确认的四相机同步帧组。低延迟预览可以跳过已经过时的整组画面，因此界面显示速度不等于数据源触发率。长期稳定性要同时看四路计数、Agent 日志和是否有丢帧。",
        "info",
    )]
    story += [H2("4.2 放大查看")]
    story += [bullets([
        "单击任意相机画面打开独立放大窗口；底部四路缩略图用于切换目标相机。",
        "鼠标滚轮：缩放；按住左键拖动：平移；双击：适应窗口。",
        "顶部“100%”按原始像素显示，“适应窗口”显示完整画面，“+ / -”逐级缩放。",
        "关闭放大窗口不会停止采集。",
    ])]
    story += [H2("4.3 常见画面现象")]
    story += [table(
        ["现象", "可能原因", "建议操作"],
        [
            ["画面噪点多", "增益过高或光照不足", "在曝光页降低最高增益，优先增加曝光或补光"],
            ["高光发绿/发紫", "局部通道过曝、自动白平衡处于极端场景", "降低最高曝光/目标亮度，避免大面积强光直射，观察实际曝光"],
            ["人物偏紫", "高光比例、自动白平衡统计或场景切换", "先降低曝光与增益；固定光源测试后再判断颜色算法"],
            ["启动时短暂旧帧", "设备或主机缓存未完全清空", "使用最新版 Viewer/Agent；确认新采集帧号重新起步"],
            ["黑屏但计数增长", "JPEG 解码或显示路径异常", "打开日志；保存原始 JPEG 诊断，不要先改曝光"],
        ], [39 * mm, 58 * mm, 67 * mm]
    )]

    story += [PageBreak(), H1("5. 相机流持久配置")]
    story += [P("相机标签右侧选择“相机流”，用于设置保存在设备上的帧率与 MJPEG 质量。该设置与“曝光”页的运行时参数不同。")]
    story += [screenshot_figure(
        "redacted/02-camera-stream-redacted.png",
        "图 5-1  Camera - Stream：左侧为四路预览与完整帧组 FPS，右侧读取和保存设备端 FPS、JPEG 质量。为保护隐私，四路实拍区域已经像素化打码。",
    )]
    story += [H2("操作步骤")]
    story += [numsteps([
        ("停止采集", "相机帧率和 JPEG 质量只有在所有流停止时才能修改。"),
        ("刷新设置", "单击“刷新设置”，读取设备当前持久配置和 generation。"),
        ("设置帧率", "输入 1 到 30 的任意整数 FPS。"),
        ("设置质量", "JPEG 质量范围 1 到 99，默认值 88。数值越高，细节和数据量通常越高。"),
        ("保存", "单击“保存设置”。设置写入设备并在下一次启动相机流水线时生效。"),
        ("重新采集验证", "开始采集，确认四路 FPS 和画面质量符合预期。"),
    ])]
    story += [table(
        ["参数", "范围 / 默认", "影响"],
        [
            ["Camera FPS", "1-30，默认 30", "决定触发周期和允许的最高曝光时间"],
            ["MJPEG 质量", "1-99，默认 88", "影响 JPEG 文件大小、细节和传输/存储压力"],
            ["持久化", "是", "设备重启后保留；下一次流水线启动生效"],
        ], [42 * mm, 44 * mm, 78 * mm]
    )]
    story += [callout(
        "质量不决定固定编码时间",
        "MJPEG 编码耗时不是完全固定的，会受图像内容、质量、ISP/编码器负载与内存带宽影响。提高质量通常增大输出，联合 LiDAR 时应通过实际 FPS 和长时间测试验收。",
        "warn",
    )]

    story += [PageBreak(), H1("6. 运行时曝光与增益")]
    story += [P("相机标签右侧选择“曝光”。这里的设置允许在采集中实时读取和修改，但仅在当前 Agent 运行期有效，设备重启后恢复默认。")]
    story += [screenshot_figure(
        "redacted/03-camera-exposure-redacted.png",
        "图 6-1  Camera - Exposure：统一目标亮度位于右侧上方；四路相机分别选择 PL 自动或手动模式，并设置曝光和 SC130GS 模拟增益。实拍区域已经打码。",
    )]
    story += [H2("6.1 自动曝光参数")]
    story += [table(
        ["参数", "含义", "建议"],
        [
            ["统一目标亮度", "四路自动曝光共用的目标，范围 1-255，默认 35", "室内先用默认值；大面积高光时适当降低"],
            ["最低曝光时间", "自动控制和手动设置可用的下限，硬件下限 50 微秒", "高速运动或强光场景提高此值前要谨慎"],
            ["最高曝光时间", "用户设定的上限", "受当前 FPS 的实际上限再次限制"],
            ["最低增益", "自动和手动增益下限，最低 1x", "默认 1x 可获得较低噪声"],
            ["最高增益", "自动和手动增益上限，最高约 124x，步进 1/32x", "室内可限制到较低值以控制噪声"],
        ], [38 * mm, 67 * mm, 59 * mm]
    )]
    story += [callout(
        "实际最高曝光公式",
        "实际最高曝光时间 = min（用户设置的最高曝光，1000000 / 当前 FPS - 5000 微秒）。例如 30 FPS 时约为 28333 微秒；10 FPS 时约为 95000 微秒。界面会直接显示当前 FPS 下的有效上限。",
        "info",
    )]
    story += [H2("6.2 自动控制顺序")]
    story += [P("PL 根据实际 RAW 画面统计调节亮度：需要增亮时先增加曝光，达到曝光上限后才提高增益；需要降低亮度时先降低增益，再缩短曝光。这样有利于控制噪声，但曝光越长运动模糊越明显。")]
    story += [H2("6.3 每路相机模式")]
    story += [bullets([
        "PL 自动曝光：该路的手动曝光框不可编辑，增益由自动控制在上下限内调节。",
        "手动：可以设置该路曝光时间与 SC130GS 模拟增益；输入值必须位于公共上下限内。",
        "四路可以混合使用自动和手动模式。做标定时通常固定曝光和增益，保证不同帧的成像一致。",
        "设置后单击“应用运行时设置”；单击“刷新曝光设置”可重新读取设备实际值。",
    ])]
    story += [callout(
        "室内与室外不是同一组最佳参数",
        "Viewer 的自动控制会根据真实画面变化，但最高曝光、最高增益和目标亮度仍由用户限定。室外强光应降低目标亮度或曝光上限；室内暗场若限制增益过低，画面可能偏暗。切换场景后应观察元数据中的实际曝光和画面高光。",
        "warn",
    )]

    story += [PageBreak(), H1("7. 相机元数据")]
    story += [P("相机工具区选择“元数据”，可查看当前完整帧组的设备侧信息。Viewer 最多每约 200 ms 更新一次显示，以减少界面开销。")]
    story += [screenshot_figure(
        "redacted/04-camera-metadata-redacted.png",
        "图 7-1  Camera - Metadata：用于查看完整帧组、触发时间、载体尺寸和四路实际曝光。实拍区域已经打码。",
    )]
    story += [table(
        ["字段", "用途"],
        [
            ["valid / cameras", "元数据结构与 CRC 是否有效、相机数量"],
            ["host_frame_id / carrier_frame_id", "主机帧号与载体帧号，用于判断是否连续"],
            ["carrier_width_bytes", "载体行宽字节数"],
            ["image_height_per_camera", "每路相机图像高度"],
            ["meta_row_bytes", "元数据行字节数"],
            ["trigger_time_ns", "公共 TRIG0 触发时刻；正常录制用它作为四路相机时间戳"],
            ["camera0...3 actual exposure", "四路实际曝光时间，单位微秒"],
            ["meta_crc32", "元数据 CRC32"],
        ], [60 * mm, 104 * mm]
    )]
    story += [callout(
        "valid 不等于时间已经同步",
        "valid=1 只表示元数据本身有效。是否可用于严格数据集，还要看“设备信息”中的 sensor-board 时间同步状态。录制器不会用主机到达时间替代未同步的传感器时间。",
        "warn",
    )]

    story += [PageBreak(), H1("8. 板载 IMU 查看与单位切换")]
    story += [P("IMU 标签显示 sensor-board 实际检测到的板载 IMU。标准产品默认标配单 IMU，仅显示 IMU0；双 IMU 版本会同时显示 IMU0 和 IMU1。采集启动后，状态表、曲线和采样率会持续更新。")]
    story += [callout(
        "双 IMU 演示截图",
        "图 8-1 来自双 IMU 演示机，因此包含 IMU0 和 IMU1 两行及两个曲线选择器。标准单 IMU 产品只显示 IMU0；这属于正常配置差异，不应判断为缺失或故障。",
        "info",
    )]
    story += [screenshot_figure(
        "05-imu.jpeg",
        "图 8-1  双 IMU 演示机的 IMU 页面：顶部选择 IMU0/IMU1 及显示单位，中间表格显示采样计数、频率、时间戳、六轴数据与同步标志，下方为实时曲线。标准产品默认仅有 IMU0。",
        max_height=86 * mm,
    )]
    story += [H2("操作")]
    story += [bullets([
        "通过已检测到的 IMU 选择器控制曲线显示内容，不影响设备实际采集。标准单 IMU 产品只有 IMU0 选择器。",
        "加速度单位、角速度单位和温度单位可在界面切换；单位选择保存在 Viewer 本地偏好中。",
        "观察 Received、Hz、Timestamp、FSYNC/同步标志。每个已检测到的 IMU 采样计数和速率都应稳定增长。",
        "若时间同步标志在启动初期短暂未就绪，等待重新锚定；录制必须等所有已检测到的板载 IMU 都已同步。",
    ])]
    story += [H2("用于标定时的时间语义")]
    story += [P("相机时间戳对应 TRIG0 曝光开始沿；板载 IMU 时间戳对应 IMU 样本时间。相机有效成像时刻通常接近曝光中心，IMU 数字滤波也会产生有效信号延迟，因此 Kalibr 的 cam-to-imu timeshift 不应直接解释为两套时钟未同步。")]
    story += [callout(
        "录制门槛",
        "Viewer 要求 sensor-board 和设备实际检测到的全部板载 IMU 同步到共同设备时间域。标准产品检查 IMU0；双 IMU 版本还要检查 IMU1。未同步样本只计入丢弃统计，不写入 v6 数据集。",
        "ok",
    )]

    story += [PageBreak(), H1("9. LiDAR 点云预览")]
    story += [screenshot_figure(
        "06-lidar.jpeg",
        "图 9-1  LiDAR 页面：上方选择是否随采集启用雷达、型号、点大小和视角；中间配置 end0；下方为点云视图。截图中的 Saved 表示网络配置已保存。",
    )]
    story += [H2("9.1 启用雷达")]
    story += [numsteps([
        ("确认物理与网络", "雷达已供电，并通过 end0 与 RK 连接。默认示例：RK 192.168.1.5/24，Mid360 192.168.1.3。"),
        ("勾选启用", "在 LiDAR 标签勾选“采集时启用雷达”。"),
        ("选择型号", "明确选择 Mid-360 或 Mid-360S。Agent 不会自动猜测型号。"),
        ("开始采集", "返回顶部单击“开始采集”。状态区应显示已连接、正在接收，并持续更新点数和批次数。"),
    ])]
    story += [H2("9.2 点云视图操作")]
    story += [table(
        ["操作", "效果"],
        [
            ["左键拖动", "旋转点云；俯仰角限制在有效观察范围内"],
            ["鼠标滚轮", "缩放，范围约 5-240 像素/米"],
            ["点大小", "调整实时渲染点大小，偏好保存在 Viewer 本地"],
            ["俯视", "切换为从上向下查看 XY 平面"],
            ["重置视角", "恢复默认旋转和缩放"],
        ], [44 * mm, 120 * mm]
    )]
    story += [P("Viewer 为保持交互性能，缓存最近约 120000 个点，单次绘制最多约 40000 个点。该限制只影响预览，不修改设备发送的数据或数据集内容。")]
    story += [callout(
        "看不到点云时",
        "先确认型号选择正确、雷达网络测试通过、LiDAR 状态 connected/receiving、点数持续增长。若雷达 IMU 有数据但点云为 0，可先对雷达执行正常软重启，再查看 Agent 日志和 56300 端口数据。",
        "warn",
    )]

    story += [PageBreak(), H1("10. LiDAR end0 网络设置")]
    story += [P("此区域配置 RK 的 end0 地址和雷达目标地址。操作发生在 RK 端，不依赖 macOS/Windows 主机本身是否能 ping 到雷达。")]
    story += [numsteps([
        ("停止采集", "所有数据流必须停止。"),
        ("刷新", "读取设备当前保存的 end0、掩码和 Mid360 地址。"),
        ("编辑", "勾选“启用 end0”，填写 RK end0 IPv4、子网掩码和 Mid360 IPv4。两者必须在同一子网且地址不能冲突。"),
        ("保存并应用", "写入持久配置并重新应用 end0。黄色 saved 表示设置已保存，但尚未做本次连接测试。"),
        ("测试连接", "单击“测试连接”。RK 会通过 end0 执行一次 ICMP ping；成功后状态变绿。"),
    ])]
    story += [callout(
        "黄色不是失败",
        "“saved”或“尚未通过连接测试”表示本次 Agent 进程内的 reachability 状态尚未确认。保存、应用或 Agent 重启都会清空该临时结果。真正 ping 失败会显示红色不可达错误。",
        "info",
    )]
    story += [callout(
        "连接测试的边界",
        "ICMP 成功只证明地址可达，不等于点云 UDP、控制 ACK 和采样都正常；反过来，某些设备不响应 ICMP 时也可能产生假阴性。最终仍要以 LiDAR 状态和点数增长为准。",
        "warn",
    )]

    story += [PageBreak(), H1("11. Wi-Fi 热点管理")]
    story += [P("“网络”标签用于查看和管理 RK 上的 5 GHz Wi-Fi 热点。该功能只管理设备网络，不是通过 Wi-Fi 传输 Viewer 实时画面的开关。")]
    story += [screenshot_figure(
        "07-network.jpeg",
        "图 11-1  Network 页面：显示无线网卡、热点运行状态、SSID、安全性、设备地址、DHCP 与开机启用状态；操作按钮位于状态区下方。",
    )]
    story += [table(
        ["字段", "说明"],
        [
            ["Wi-Fi 设备 / 接口", "是否检测到无线网卡及接口名称"],
            ["热点状态", "运行中、部分运行、已启用但未运行、已关闭"],
            ["SSID", "热点名称，通常与设备序列号相关"],
            ["安全性", "当前界面显示开放网络，无密码"],
            ["本机地址", "默认 10.42.200.1/24"],
            ["DHCP", "地址分配服务状态"],
            ["开机启用", "配置是否持久化并在启动时开启"],
        ], [48 * mm, 116 * mm]
    )]
    story += [H2("操作步骤")]
    story += [bullets([
        "停止 Camera、IMU 和 LiDAR 传输。",
        "单击“刷新状态”读取实时状态。",
        "单击“开启热点”或“关闭热点”。启用/禁用会保存到设备。",
        "热点切换可能让当前 SSH 或网络会话断开；USB Viewer 会话不应依赖该 Wi-Fi 路径。",
    ])]
    story += [callout(
        "开放网络提醒",
        "当前热点没有密码。仅在受控环境使用，避免在开放场所传输敏感数据。不要在 SDK/Viewer 通过设备 Wi-Fi 接口进行管理操作时切换热点。",
        "danger",
    )]

    story += [PageBreak(), H1("12. 数据集录制")]
    story += [H2("12.1 录制前检查")]
    story += [bullets([
        "采集已经启动，四路 Camera 与所有已检测到的板载 IMU 数据持续增长。",
        "sensor-board 和所有已检测到的板载 IMU 均显示时间已同步。",
        "若需要 LiDAR，已经在开始采集前勾选并选择正确型号，点云和雷达 IMU 均有数据。",
        "目标磁盘空间充足。四路 JPEG 和点云长时间录制会快速增长。",
    ])]
    story += [H2("12.2 两种录制模式")]
    story += [table(
        ["模式", "未启用 LiDAR", "启用 LiDAR"],
        [
            ["完整数据集", "四路 Camera + 已检测到的板载 IMU（标准版为 IMU0）", "再加入点云 + 雷达内置 IMU"],
            ["仅录制 IMU", "已检测到的板载 IMU（标准版为 IMU0）", "再加入雷达内置 IMU；不录 Camera 和点云"],
        ], [38 * mm, 59 * mm, 67 * mm]
    )]
    story += [numsteps([
        ("选择模式", "单击“录制...”右侧菜单，选择“完整数据集”或“仅录制 IMU”。"),
        ("选择目录", "选择一个空目录。若目录已有 Prism 数据集，Viewer 会明确询问是否覆盖。"),
        ("观察计数", "顶部和日志显示录制状态、各流写入计数及丢弃统计。"),
        ("停止录制", "单击“停止录制”，等待 manifest 完成并关闭文件。随后才能停止采集。"),
    ])]
    story += [callout(
        "数据集必须整体移动",
        "相机 .tum 和 lidar.tum 只是索引，实际 JPEG/点云存放在 camera-data-NNNN.bin 与 lidar-data-NNNN.bin。不要只复制 .tum，也不要单独重命名容器文件。始终移动或归档整个目录。",
        "danger",
    )]
    story += [H2("12.3 完成状态")]
    story += [P("录制开始时 dataset.info 写入 complete=0。只有停止录制、关闭文件并确认必需数据流非空后，才原子写成 complete=1。意外断电、强制退出或直接拔盘可能留下未完成数据集。")]

    story += [PageBreak(), H1("13. 本地数据集回放与验证")]
    story += [screenshot_figure(
        "08-local-datasets.jpeg",
        "图 13-1  Local Datasets：顶部打开、验证和导出数据集；中间控制上一帧、播放、下一帧与速度；四路区域显示已录制画面。",
    )]
    story += [H2("13.1 打开和浏览")]
    story += [numsteps([
        ("进入本地数据集", "单击“本地数据集”标签，再单击“打开数据集...”。"),
        ("选择目录", "选择包含 dataset.info、imu0.tum 等文件的根目录；双 IMU 数据集还包含 imu1.tum。"),
        ("浏览帧", "使用“上一帧 / 播放 / 暂停 / 下一帧”、时间滑块和速度选择器。"),
        ("改变速度", "支持 0.25x、0.5x、1x、2x、4x、8x。播放按数据集时间戳间隔变速，不是固定墙钟帧率。"),
        ("放大图像", "单击四路数据集图像，使用与实时画面相同的放大窗口。"),
    ])]
    story += [H2("13.2 验证数据集")]
    story += [P("单击“验证数据集...”，Viewer 会检查清单、必需数据流、时间戳单调性与跳变、容器范围、JPEG 负载、LiDAR 记录长度和同步来源。结果分为有效、有效但有警告、无效。")]
    story += [table(
        ["结果", "含义", "建议"],
        [
            ["有效", "格式与数据一致", "可继续回放或导出"],
            ["有效但有警告", "主体可读，但有时间戳或数据异常", "展开详细报告，判断是否满足算法要求"],
            ["无效", "缺少必需文件、容器越界、时间戳/同步契约不满足等", "不要直接用于标定或算法；保留原始目录排查"],
        ], [35 * mm, 63 * mm, 66 * mm]
    )]
    story += [callout(
        "时间戳跳变检查",
        "验证器不仅检查是否递增，还统计中位、最小、最大间隔及 discontinuities。即使图像可以显示，只要时间戳异常，也可能不适合 Kalibr、SLAM 或传感器融合。",
        "warn",
    )]

    story += [PageBreak(), H1("14. 导出 ROS1 / ROS2 Bag")]
    story += [P("打开有效数据集后，单击“导出 ROS Bag...”菜单，选择 ROS1 Bag（.bag）或 ROS2 Bag（SQLite3）。转换由 Viewer 完成，主机无需安装 ROS。")]
    story += [numsteps([
        ("停止实时采集和录制", "导出按钮只在本地数据集已加载、无实时采集、无录制作业时可用。"),
        ("选择格式", "ROS1 输出单个 .bag；ROS2 输出 .rosbag2 目录，包含 metadata.yaml 和 .db3。"),
        ("选择目标", "若目标已存在，Viewer 会询问是否在转换成功后替换。"),
        ("等待转换", "进度窗口显示阶段和记录数，可取消。成功后显示 Camera、IMU、LiDAR 消息统计。"),
    ])]
    story += [table(
        ["数据", "ROS 话题"],
        [
            ["Camera 0-3 JPEG", "/prism/cameraN/image/compressed"],
            ["板载 IMU0", "/prism/imu0/data"],
            ["板载 IMU1（仅双 IMU 版本）", "/prism/imu1/data"],
            ["LiDAR 点云", "/prism/lidar/points"],
            ["LiDAR 内置 IMU", "/prism/lidar/imu/data"],
        ], [60 * mm, 104 * mm]
    )]
    story += [callout(
        "文件安全",
        "转换先写临时文件或临时目录，完成后才替换已有输出。取消或失败不会破坏原有 Bag。输出路径不能覆盖源 Prism 数据集目录。",
        "ok",
    )]

    story += [PageBreak(), H1("15. Prism 系统升级")]
    story += [callout(
        "高风险操作",
        "只使用可信的 Prism 系统升级 ZIP。升级前接稳定电源，停止所有流和录制。升级过程中禁止断电、拔 USB、关闭 Viewer 或让电脑休眠。",
        "danger",
    )]
    story += [numsteps([
        ("确认空闲", "停止录制和采集，确认设备仍保持打开。"),
        ("单击系统升级", "选择 *.zip。Viewer 会先检查包中是否同时包含匹配的 Agent 镜像和 sensor-board BOOT.BIN。"),
        ("核对版本", "确认对话框中的升级包版本、Agent 版本、sensor-board 版本。默认选择“否”，必须主动确认。"),
        ("执行升级", "Viewer 显示 validating、agent、sensor-board、complete 等阶段和进度。不要干预。"),
        ("等待重启", "升级成功后设备会重启，Viewer 关闭 USB 会话。等待设备重新枚举后刷新、打开。"),
        ("验收", "刷新版本和设备信息，确认 Agent/sensor-board 版本、USB 3、传感器在线、时间同步和错误标志。再做短时 Camera/IMU/LiDAR 测试。"),
    ])]
    story += [P("升级实现包含包验证、组件传输、sensor-board QSPI 回读验证和重启。若失败，保留完整日志和包名，不要连续反复刷写。")]

    story += [PageBreak(), H1("16. 日志、语言与关闭流程")]
    story += [H2("16.1 日志")]
    story += [bullets([
        "单击“打开日志”查看设备打开、同步、采集、录制、网络和升级事件。",
        "Auto-scroll 自动跟随最新日志；Copy All 复制全部内容；Clear 只清空 Viewer 当前窗口；Close 关闭日志窗口。",
        "提交故障时同时提供时间、Viewer/Agent/sensor-board 版本、USB 速率、操作步骤和完整日志。",
    ])]
    story += [H2("16.2 语言")]
    story += [P("设备关闭且没有任务运行时，可在顶部选择“中文”或“English”。Viewer 会自动重启一次并保存偏好。")]
    story += [H2("16.3 正确关闭")]
    story += [numsteps([
        ("停止录制", "等待数据集完成和文件关闭。"),
        ("停止采集", "等待 Camera/IMU/LiDAR 状态停止。"),
        ("关闭设备", "释放 USB 访问权。"),
        ("退出 Viewer", "最后关闭应用。"),
    ])]

    story += [PageBreak(), H1("17. 常见问题排查")]
    story += [table(
        ["问题", "原因判断", "处理顺序"],
        [
            ["Open Device 被拒绝 / LIBUSB_ERROR_ACCESS", "设备被另一个 Viewer、SDK 工具或进程占用，或 Linux 权限不足", "关闭其他程序；重新插拔；Linux 安装 udev 规则并重新登录"],
            ["LIBUSB_ERROR_OVERFLOW", "SDK 与 Agent 帧读取方式不匹配、USB 传输异常或旧运行库", "确认 Viewer/SDK/Agent 版本一致；直连 USB 3；保存日志并重试"],
            ["只有 USB 2 速率", "线缆、接口或 Type-C 硬件链路问题", "更换已验证 USB 3 线和端口；检查 ESD/布线；设备信息确认 super-speed"],
            ["Camera+LiDAR 不满 30 FPS", "ISP/JPEG 负载、DDR/IRQ、MJPEG 数据量或旧 Agent", "确认满性能镜像、默认 TALL 与正确质量；查看四路 FPS、Agent 日志和 IRQ；做 60 秒 A/B"],
            ["Camera 3/4 曝光显示 0", "元数据未更新、固件/解析版本不一致", "刷新版本；确认 1.0.0 组合；检查 Metadata valid 和实际曝光"],
            ["点云不显示", "未选型号、网络未通、雷达仅 IMU 出流或固件状态异常", "选正确型号；测试 end0；看 connected/receiving/points；软重启雷达"],
            ["雷达状态黄条 saved", "保存后 reachability 尚未重新测试", "单击“测试连接”；这不是实际失败"],
            ["数据集验证失败", "complete=0、缺文件、时间戳跳变、同步标志无效", "查看详细报告；不要手工移动单个容器；保留原始目录"],
            ["时间差固定 37 秒", "错误使用 LiDAR raw PTP/TAI 时间而非归一化时间，或版本组合过旧", "使用最新 1.0.0 组件；新数据集第一列应使用共同设备时间；检查同步标志"],
            ["系统升级失败", "包不匹配、供电/USB 中断、组件回读失败", "不要继续采集；复制日志；核对包版本和设备状态后再决定回滚或重试"],
        ], [40 * mm, 58 * mm, 66 * mm], small=True
    )]
    story += [H2("排障时不要同时改变多个变量")]
    story += [P("例如 Camera+LiDAR FPS 异常时，先固定场景、FPS、JPEG 质量和曝光，只改变一个系统版本或性能配置；记录 60 秒四路完整帧组与点云计数。一次同时改曝光、质量、IRQ 和固件无法定位根因。")]

    story += [PageBreak(), H1("18. 操作条件与持久化速查")]
    story += [table(
        ["功能", "是否要求停止采集", "是否持久化", "生效时间"],
        [
            ["打开/关闭设备", "关闭前必须停止", "否", "立即"],
            ["校准设备时间", "是", "写 RK/PHC/RTC", "同步完成后"],
            ["Camera FPS / JPEG 质量", "是", "是", "下次相机流水线启动"],
            ["运行时曝光/增益/上下限", "否，可采集中修改", "否", "应用后立即"],
            ["LiDAR 是否启用/型号", "开始采集前选择", "Viewer 偏好部分保留", "本次采集"],
            ["LiDAR end0 网络", "是", "是", "保存并应用后"],
            ["Wi-Fi 热点", "是", "是", "操作完成后，开机策略同步更新"],
            ["录制", "必须在采集中", "写数据集", "选择目录后"],
            ["数据集验证/ROS 导出", "是，不得实时采集", "写报告/Bag", "任务完成后"],
            ["系统升级", "是", "是", "组件重启后"],
            ["语言", "设备关闭且空闲", "Viewer 本地", "Viewer 自动重启后"],
        ], [45 * mm, 45 * mm, 34 * mm, 40 * mm], small=True
    )]
    story += [H2("采集验收建议")]
    story += [bullets([
        "四路 Camera FPS 接近目标值，四路完整帧组计数差不超过 1。",
        "所有已检测到的板载 IMU 采样率稳定，时间同步标志有效，无持续丢样；标准产品默认只检查 IMU0。",
        "启用 LiDAR 时点数、批次数和 LiDAR IMU 持续增长，无小包异常和 dropped 增长。",
        "长时间测试结束后 camera_streaming_mask 回到 0，IMU 接收停止，sensor-board error_flags 为 0。",
        "录制数据集先通过 Viewer 验证，再用于 Kalibr、SLAM 或 ROS。",
    ])]
    story += [callout(
        "文档边界",
        "本手册描述 Viewer 操作，不替代硬件装配、传感器标定、SDK 开发和系统镜像烧录文档。若界面与本文不同，请先在“设备信息”核对实际版本。",
        "info",
    )]

    return story


def main():
    OUT.parent.mkdir(parents=True, exist_ok=True)
    doc = ManualDocTemplate(str(OUT))
    doc.multiBuild(build_story())
    print(OUT)


if __name__ == "__main__":
    main()
