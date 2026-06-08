#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
make_cn_points_xlsx.py —— 生成一份"中文工业点表"示例 examples/cn_points.xlsx。

表头和现场常见的样子一致：
  点位ID | 点位名称 | 点位地址 | 数据类型 | NodeID | 单位 | 最新数据 | 更新时间 | 设备号

地址(点位地址)故意对准 config/fake_plc.json 里 fake_plc 的 DB1 实际布局，
这样可以拿它做端到端验证：cn_points.xlsx --(xlsx_to_config.py)--> JSON --> gateway --> 读到真值。

其中"高精度压力"一行故意用 DB1.DBD22 配 float64(4字节字母 D 配 8字节类型)，
用来演示脚本的"宽度不一致"提示。

用法：python3 examples/make_cn_points_xlsx.py [输出路径]
"""

import sys
import openpyxl

HEADER = ["点位ID", "点位名称", "点位地址", "数据类型", "NodeID",
          "单位", "最新数据", "更新时间", "设备号"]

# 点位ID, 点位名称, 点位地址, 数据类型, NodeID, 单位, 最新数据, 更新时间, 设备号
ROWS = [
    [1001001, "炉顶南侧压力", "DB1.DBD0",  "float32", "ns=2;s=[1001001]", "kPa", 2.5,   "2025/6/1 12:32", "DE-1202323"],
    [1001002, "电机电流",     "DB1.DBD4",  "float32", "ns=2;s=[1001002]", "A",   12.3,  "2025/6/1 12:32", "DE-1202323"],
    [1001003, "电机运行",     "DB1.DBX8.0", "bool",   "ns=2;s=[1001003]", "",    True,  "2025/6/1 12:32", "DE-1202323"],
    [1001004, "电机故障",     "DB1.DBX8.1", "bool",   "ns=2;s=[1001004]", "",    False, "2025/6/1 12:32", "DE-1202323"],
    [1001005, "加热器开",     "DB1.DBX8.2", "bool",   "ns=2;s=[1001005]", "",    False, "2025/6/1 12:32", "DE-1202323"],
    [1001006, "炉温",         "DB1.DBW10", "int16",   "ns=2;s=[1001006]", "℃",   77,    "2025/6/1 12:32", "DE-1202323"],
    [1001007, "产量计数",     "DB1.DBD12", "int32",   "ns=2;s=[1001007]", "件",  1013,  "2025/6/1 12:32", "DE-1202323"],
    [1001008, "状态字",       "DB1.DBW16", "word",    "ns=2;s=[1001008]", "",    1644,  "2025/6/1 12:32", "DE-1202323"],
    [1001009, "运行时长",     "DB1.DBD18", "dword",   "ns=2;s=[1001009]", "s",   13,    "2025/6/1 12:32", "DE-1202323"],
    [1001010, "高精度压力",   "DB1.DBD22", "float64", "ns=2;s=[1001010]", "kPa", 5.05,  "2025/6/1 12:32", "DE-1202323"],
    [1001011, "液位",         "DB1.DBD30", "float32", "ns=2;s=[1001011]", "%",   87.0,  "2025/6/1 12:32", "DE-1202323"],
]


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "examples/cn_points.xlsx"

    wb = openpyxl.Workbook()
    ws = wb.active
    ws.title = "点表"
    ws.append(HEADER)
    for r in ROWS:
        ws.append(r)
    wb.save(out)
    print(f"✅ 已生成中文点表示例 {out}（{len(ROWS)} 个点位，地址对准 fake_plc 的 DB1）")


if __name__ == "__main__":
    main()
