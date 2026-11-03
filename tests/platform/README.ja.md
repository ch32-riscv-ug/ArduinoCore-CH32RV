# platform

実機を使わない source・生成物・compile の契約検査を置きます。
現在の入口は test_source_locks.py で、固定 source の一覧と hash をオフラインで確認します。
upstream の由来、API 互換、CH32 compile、実機動作の保証ではありません。

toolchain を必要とする検査を追加する場合は前提を明示します。
生成物の確認と通常のテストはリポジトリを変更せず、build は隔離した作業領域に出力します。
