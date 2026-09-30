#include <Instances/Program.hpp>
#include <Core/PhysicalFileInstanceRegistry.hpp>
#include <Util/AssetGuard.hpp>
#include <Util/AssetPath.hpp>
#include <Util/Logger.hpp>
#include <Util/Platform.hpp>

#include <algorithm>
#include <filesystem>
#include <vector>

namespace {
std::vector<Program*>& liveInstances() {
    static std::vector<Program*> instances;
    return instances;
}
}

Program::Program() : PhysicalFileInstance("Program") {
    Name = "Program";
    liveInstances().push_back(this);
}

Program::~Program() {
    forceClose();
    auto& instances = liveInstances();
    instances.erase(std::remove(instances.begin(), instances.end(), this), instances.end());
}

void Program::forceCloseAll() {
    const std::vector<Program*> snapshot = liveInstances();
    for (Program* program : snapshot) program->forceClose();
}

void Program::pollAll() {
    // poll中にProgramが破棄されても安全なようコピーを走査する。
    const std::vector<Program*> snapshot = liveInstances();
    for (Program* program : snapshot) program->poll();
}

bool Program::IsA(std::string className) {
    return className == "Program" || PhysicalFileInstance::IsA(std::move(className));
}

std::shared_ptr<Instance> Program::clone() const {
    // プロセスは複製しない。Pathだけを引き継ぐ未起動のProgramを返す。
    auto copy = std::make_shared<Program>();
    clonePhysicalFileFieldsTo(*copy, "Program");
    return copy;
}

std::string Program::encodeMessage(const std::string& message) {
    std::string encoded;
    encoded.reserve(message.size());
    for (const char c : message) {
        switch (c) {
        case '\\': encoded += "\\\\"; break;
        case '\n': encoded += "\\n";  break;
        case '\r': encoded += "\\r";  break;
        default:   encoded += c;      break;
        }
    }
    return encoded;
}

std::string Program::decodeMessage(const std::string& line) {
    std::string decoded;
    decoded.reserve(line.size());
    for (size_t i = 0; i < line.size(); ++i) {
        if (line[i] != '\\' || i + 1 >= line.size()) {
            decoded += line[i];
            continue;
        }
        const char next = line[++i];
        switch (next) {
        case 'n':  decoded += '\n'; break;
        case 'r':  decoded += '\r'; break;
        case '\\': decoded += '\\'; break;
        default:   decoded += '\\'; decoded += next; break;  // 未知のエスケープはそのまま残す
        }
    }
    return decoded;
}

bool Program::start(std::string& error) {
    if (m_process) {
        error = "Program is already started; call Close first";
        return false;
    }
    if (Path.empty()) {
        error = "Program.Path is empty";
        return false;
    }
    if (!AssetGuard::allow(Path)) {
        error = "Program path denied";
        return false;
    }
    const std::filesystem::path executable = AssetPath::fromStored(Path);
    std::error_code ec;
    if (!std::filesystem::is_regular_file(executable, ec)) {
        error = "Program executable not found: " + Path;
        return false;
    }
    ChildProcessLaunchOptions options;
    options.executable = executable.string();
    auto process = getPlatform().launchPipedProcess(options);
    if (!process) {
        error = "failed to start program (unsupported platform or launch failure)";
        return false;
    }
    return attachProcess(std::move(process));
}

bool Program::attachProcess(std::unique_ptr<IPipedProcess> process) {
    if (m_process || !process) return false;
    m_process = std::move(process);
    m_connected = true;
    return true;
}

bool Program::writeRequest(const std::string& request, std::string& error) {
    if (!isConnected()) {
        error = isStarted() ? "Program is disconnected" : "Program is not started";
        return false;
    }
    if (m_closing) {
        error = "Program is closing";
        return false;
    }
    const std::string encoded = encodeMessage(request);
    if (encoded.size() > MAX_MESSAGE_BYTES) {
        error = "request exceeds 1 MiB";
        return false;
    }
    if (!m_process->write(encoded + "\n")) {
        error = "failed to write to program (process exited?)";
        return false;
    }
    return true;
}

bool Program::send(const std::string& request, std::string& error) {
    if (!writeRequest(request, error)) return false;
    m_expected.push_back({ResponseTarget::Receive, 0});
    return true;
}

uint64_t Program::beginCall(const std::string& request, std::string& error) {
    if (!writeRequest(request, error)) return 0;
    const uint64_t callId = m_nextCallId++;
    m_calls[callId] = CallState{};
    m_expected.push_back({ResponseTarget::Call, callId});
    return callId;
}

Program::CallStatus Program::pollCall(uint64_t callId, std::string& responseOrError) {
    const auto it = m_calls.find(callId);
    if (it == m_calls.end()) {
        responseOrError = "program was closed";
        return CallStatus::Failed;
    }
    if (it->second.status == CallStatus::Pending) return CallStatus::Pending;
    const CallStatus status = it->second.status;
    responseOrError = std::move(it->second.value);
    m_calls.erase(it);
    return status;
}

void Program::abandonCall(uint64_t callId) {
    m_calls.erase(callId);
    for (ExpectedResponse& expected : m_expected) {
        if (expected.target == ResponseTarget::Call && expected.callId == callId)
            expected.target = ResponseTarget::Discard;
    }
}

std::optional<std::string> Program::receive(std::string& error) {
    if (!isConnected()) {
        error = isStarted() ? "Program is disconnected" : "Program is not started";
        return std::nullopt;
    }
    if (m_received.empty()) return std::nullopt;
    std::string message = std::move(m_received.front());
    m_received.pop_front();
    return message;
}

bool Program::connect(std::string& error) {
    if (!isStarted()) {
        error = "Program is not started";
        return false;
    }
    if (m_closing) {
        error = "Program is closing";
        return false;
    }
    if (m_connected) {
        error = "Program is already connected";
        return false;
    }
    m_connected = true;
    return true;
}

bool Program::disconnect(std::string& error) {
    if (!isConnected()) {
        error = isStarted() ? "Program is already disconnected" : "Program is not started";
        return false;
    }
    m_connected = false;
    failPendingCalls("program was disconnected");
    // 切断前の要求に対する応答は、Callの分だけ破棄し、Sendの分は受信キューに残す。
    for (ExpectedResponse& expected : m_expected) {
        if (expected.target == ResponseTarget::Call) expected.target = ResponseTarget::Discard;
    }
    return true;
}

bool Program::beginClose(std::chrono::duration<double> grace, std::string& error) {
    if (!isStarted()) {
        error = "Program is not started";
        return false;
    }
    if (m_closing) {
        error = "Program is already closing";
        return false;
    }
    m_closing = true;
    m_connected = false;
    failPendingCalls("program is closing");
    m_process->closeStdin();
    m_closeDeadline = std::chrono::steady_clock::now() +
        std::chrono::duration_cast<std::chrono::steady_clock::duration>(grace);
    return true;
}

std::optional<int> Program::pollClose() {
    if (!m_process || !m_closing) return std::nullopt;
    poll();
    if (!m_process->isRunning()) {
        const int code = m_process->exitCode().value_or(-1);
        resetProcessState();
        return code;
    }
    if (!m_terminateSent && std::chrono::steady_clock::now() >= m_closeDeadline) {
        RCBN_WARN("Program: process did not exit within the grace period; terminating");
        m_process->terminate();
        m_terminateSent = true;
    }
    return std::nullopt;
}

void Program::forceClose() {
    if (!m_process) return;
    m_process->closeStdin();
    m_process->terminate();
    resetProcessState();
}

void Program::poll() {
    if (!m_process) return;
    readPipe();
    if (!m_process->isRunning()) {
        readPipe();  // 終了直前に書かれた出力を取りこぼさない
        failPendingCalls("program exited");
    }
}

void Program::readPipe() {
    if (m_pipeClosed) return;
    if (!m_process->readAvailable(m_readBuffer)) m_pipeClosed = true;

    size_t begin = 0;
    for (size_t newline = m_readBuffer.find('\n', begin); newline != std::string::npos;
         newline = m_readBuffer.find('\n', begin)) {
        std::string line = m_readBuffer.substr(begin, newline - begin);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        dispatchLine(line);
        begin = newline + 1;
    }
    m_readBuffer.erase(0, begin);

    if (m_readBuffer.size() > MAX_MESSAGE_BYTES) {
        RCBN_ERROR("Program: incoming message exceeds 1 MiB without a newline; terminating process");
        m_readBuffer.clear();
        m_process->terminate();
    }
}

void Program::dispatchLine(const std::string& line) {
    std::string message = decodeMessage(line);
    ExpectedResponse expected{ResponseTarget::Receive, 0};
    if (!m_expected.empty()) {
        expected = m_expected.front();
        m_expected.pop_front();
    }
    switch (expected.target) {
    case ResponseTarget::Call: {
        const auto it = m_calls.find(expected.callId);
        if (it != m_calls.end() && it->second.status == CallStatus::Pending) {
            it->second.status = CallStatus::Ready;
            it->second.value = std::move(message);
        }
        break;
    }
    case ResponseTarget::Discard:
        break;
    case ResponseTarget::Receive:
        if (m_received.size() >= MAX_RECEIVED_LINES) {
            RCBN_WARN("Program: receive queue is full; dropping the oldest message");
            m_received.pop_front();
        }
        m_received.push_back(std::move(message));
        break;
    }
}

void Program::failPendingCalls(const std::string& reason) {
    for (auto& [callId, state] : m_calls) {
        (void)callId;
        if (state.status != CallStatus::Pending) continue;
        state.status = CallStatus::Failed;
        state.value = reason;
    }
}

void Program::resetProcessState() {
    m_process.reset();
    m_connected = false;
    m_closing = false;
    m_terminateSent = false;
    m_pipeClosed = false;
    m_readBuffer.clear();
    m_received.clear();
    m_expected.clear();
    m_calls.clear();
}

void PhysicalFileInstanceRegistry::registerProgramType() {
    static const bool registered = [] {
        PhysicalFileInstanceRegistry::registerType(
            PhysicalFileInstanceType{"Program", PhysicalFileKind::Generic,
                PhysicalFileInsertCategory::Other, "Program (*.exe)", "*.exe",
                [] { return std::make_shared<Program>(); }, false});
        return true;
    }();
    (void)registered;
}
