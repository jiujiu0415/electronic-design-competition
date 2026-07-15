# 电子设计大赛备赛工作台

大一下学期开始，目标2027年全国大学生电子设计大赛国赛。

## 目录结构

```
Electronic Design/
├── docs/               # 方法论和文档
│   └── 备赛方法论.md    # 核心思维框架
├── skills-repos/       # 已下载的技能源码仓库
├── memory/             # Claude Code 记忆存储
└── README.md
```

## 环境配置

- **主控平台**: STM32F1/F4（HAL库）
- **辅助平台**: MSP430（低功耗专项）
- **开发环境**: Keil MDK / STM32CubeIDE
- **EDA工具**: KiCad / 立创EDA
- **AI助手**: Claude Code + 34个专项技能

## 已安装技能速查

| 类别 | 数量 | 核心技能 |
|------|------|---------|
| STM32嵌入式 | 14 | stm32-baremetal, freertos, embedded-systems |
| 电路设计 | 9 | eda-schematics, spice-simulation, circuit-debugger |
| 元器件采购 | 7 | lcsc-parts, jlcpcb, bom-generator |
| 传感器测量 | 3 | sensor-calibration, power-budget-calculator |
| 元技能 | 2 | find-skills, skill-creator |

## 快速开始

```bash
# 搜索新技能
npx skills find <关键词>

# 安装技能
npx skills add <owner/repo@skill> -g -y

# SSH备用方案
git clone git@github.com:owner/repo.git
cp -r repo/skills/skill-name ~/.claude/skills/
```
