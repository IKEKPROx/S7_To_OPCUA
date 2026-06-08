#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
make_points_xlsx.py —— 生成一份示例 Excel 点表 examples/points.xlsx。

为什么有这个脚本？
  xlsx 是二进制文件，没法像 .json 那样直接看/改/进版本库 diff。
  所以用这个小脚本"用代码生成"示例点表：内容故意和 config/fake_plc.json 完全一致，
  这样就能做端到端验证——
      points.xlsx --(xlsx_to_config.py)--> JSON  应当 ≈ fake_plc.json
  以后想改示例点表，改这里再重跑即可，不用手动开 Excel。

用法：
  python3 examples/make_points_xlsx.py            # 写到 examples/points.xlsx
  python3 examples/make_points_xlsx.py 别的.xlsx   # 写到指定路径
"""

import sys
import openpyxl

# 表头：顺序故意打乱一点也没关系，xlsx_to_config.py 是按列名认的。
# 这里就按推荐顺序排。
HEADER = ["plc_name", "ip", "port", "rack", "slot", "poll_interval_ms",
          "tag_name", "area", "db", "start", "bit", "type"]

# 11 个点，和 config/fake_plc.json 一一对应(非 BOOL 行的 bit 留空)
PLC = ("FakeLine", "127.0.0.1", 1102, 0, 1, 500)   # name, ip, port, rack, slot, poll
TAGS = [
    # tag_name,        area, db, start, bit,  type
    ("Motor1_Speed",   "DB", 1, 0,  None, "REAL"),
    ("Motor1_Current", "DB", 1, 4,  None, "REAL"),
    ("Motor1_Running", "DB", 1, 8,  0,    "BOOL"),
    ("Motor1_Fault",   "DB", 1, 8,  1,    "BOOL"),
    ("Heater_On",      "DB", 1, 8,  2,    "BOOL"),
    ("Temperature",    "DB", 1, 10, None, "INT"),
    ("PartCounter",    "DB", 1, 12, None, "DINT"),
    ("StatusWord",     "DB", 1, 16, None, "WORD"),
    ("TotalRuntime",   "DB", 1, 18, None, "DWORD"),
    ("Pressure",       "DB", 1, 22, None, "LREAL"),
    ("TankLevel",      "DB", 1, 30, None, "REAL"),
]


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "examples/points.xlsx"

    wb = openpyxl.Workbook()
    ws = wb.active
    ws.title = "points"

    ws.append(HEADER)
    name, ip, port, rack, slot, poll = PLC
    for tag_name, area, db, start, bit, typ in TAGS:
        ws.append([name, ip, port, rack, slot, poll,
                   tag_name, area, db, start, bit, typ])

    wb.save(out)
    print(f"✅ 已生成示例点表 {out}（1 个 PLC、{len(TAGS)} 个点位）")


if __name__ == "__main__":
    main()
