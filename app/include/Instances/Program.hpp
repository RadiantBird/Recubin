#pragma once
#include <Instances/PhysicalFileInstance.hpp>
#include <Util/IPlatform.hpp>

#include <chrono>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <string>

// exeファイルを所有し、標準入出力パイプ越しに子プロセスと文字列を交換するInstance。
// 要求/応答は1行=1メッセージ(UTF-8、改行はエスケープ)。各要求(Send/Call)は応答を1行ずつ
// FIFOで消費する。Luau依存は持たず、コルーチンの待機はLuauEngine側が
// beginCall/pollCall/beginClose/pollCloseを使って行う。
class Program final : public PhysicalFileInstance {
public:
    enum class CallStatus { Pending, Ready, Failed };

    static constexpr size_t MAX_MESSAGE_BYTES = 1024 * 1024;
    static constexpr size_t MAX_RECEIVED_LINES = 4096;
    static constexpr double DEFAULT_CLOSE_GRACE_SECONDS = 3.0;

    Program();
    ~Program() override;

    std::string getClassName() override { return "Program"; }
    bool IsA(std::string className) override;
    std::shared_ptr<Instance> clone() const override;

    // 改行・バックスラッシュ・CRをエスケープ/復元する(末尾の改行は含まない)。
    static std::string encodeMessage(const std::string& message);
    static std::string decodeMessage(const std::string& line);

    bool start(std::string& error);
    // 起動済みのプロセスを差し込む(テスト用)。既に起動済みならfalse。
    bool attachProcess(std::unique_ptr<IPipedProcess> process);

    bool isStarted() const { return m_process != nullptr; }
    bool isConnected() const { return m_process != nullptr && m_connected; }

    bool send(const std::string& request, std::string& error);
    // 応答待ちの要求を送る。失敗時は0を返しerrorを設定する。
    uint64_t beginCall(const std::string& request, std::string& error);
    CallStatus pollCall(uint64_t callId, std::string& responseOrError);
    // タイムアウトなどで待つのをやめる。遅れて届く応答は破棄される。
    void abandonCall(uint64_t callId);

    // 受信キューの先頭を取り出す。無ければnullopt(errorは空)。状態エラー時はerrorを設定する。
    std::optional<std::string> receive(std::string& error);
    bool connect(std::string& error);
    bool disconnect(std::string& error);

    // stdinを閉じて終了を待つ。猶予を過ぎたら強制終了する。
    bool beginClose(std::chrono::duration<double> grace, std::string& error);
    // 終了していれば終了コードを返して状態を初期化する。待機中はnullopt。
    std::optional<int> pollClose();
    // 即時強制終了して状態を初期化する(破棄・停止時用)。
    void forceClose();

    // パイプを読み、応答の振り分けとプロセス終了の検出を行う。
    void poll();
    // 生存中の全Programをpollする(メインスレッドから毎フレーム)。切断中でも子の
    // 出力を吸い上げ、パイプが詰まって子がブロックするのを防ぐ。
    static void pollAll();
    // 生存中の全Programのプロセスを即時強制終了する(Play停止・シーン切替・エンジン終了時)。
    static void forceCloseAll();

private:
    enum class ResponseTarget { Receive, Call, Discard };
    struct ExpectedResponse {
        ResponseTarget target = ResponseTarget::Receive;
        uint64_t callId = 0;
    };
    struct CallState {
        CallStatus status = CallStatus::Pending;
        std::string value;  // Ready=応答 / Failed=理由
    };

    bool writeRequest(const std::string& request, std::string& error);
    void readPipe();
    void dispatchLine(const std::string& line);
    void failPendingCalls(const std::string& reason);
    void resetProcessState();

    std::unique_ptr<IPipedProcess> m_process;
    bool m_connected = false;
    bool m_closing = false;
    bool m_terminateSent = false;
    std::chrono::steady_clock::time_point m_closeDeadline{};

    std::string m_readBuffer;
    std::deque<std::string> m_received;
    std::deque<ExpectedResponse> m_expected;
    std::map<uint64_t, CallState> m_calls;
    uint64_t m_nextCallId = 1;
    bool m_pipeClosed = false;
};
