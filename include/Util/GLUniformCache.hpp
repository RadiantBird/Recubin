#pragma once

// glGetUniformLocation（文字列検索）の毎フレーム呼び出しを避けるためのキャッシュ。
// 呼び出し側は関数ローカル static CachedUniform を保持し、この関数経由で引く。
// プログラムが再リンク・差し替えされた場合は program 不一致で自動的に引き直す。
struct CachedUniform {
    unsigned int program = 0;
    int loc = -1;
};

int cachedUniformLocation(unsigned int program, CachedUniform& slot, const char* name);
