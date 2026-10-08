# ADR-0006: Core は bare metal とし RTOS を同梱しない

- Status: Accepted

## 決定

全 series で同じ bare-metal core を使い、RTOS 上に core を構築しません。RTOS の有無を board menu にも
しません。RTOS が必要な場合は、core と独立した library / port として提供します。

## 理由

対象には RAM 2 KB の型番があり、RTOS を常設できません。RTOS の有無で core の時間、割り込み、同期の
意味を分けると、実装と互換性を二系統維持することになります。独立 library なら未使用時のコストがなく、
kernel version と設定も利用者側で選べます。
