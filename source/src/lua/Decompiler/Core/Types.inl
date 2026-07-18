class Decompiler;
class Expression; using ExpressionPtr = std::shared_ptr<Expression>;
class Target;     using TargetPtr     = std::shared_ptr<Target>;
class Statement;  using StatementPtr  = std::shared_ptr<Statement>;
class Operation;  using OperationPtr  = std::shared_ptr<Operation>;
class Branch;     using BranchPtr     = std::shared_ptr<Branch>;
class Block;      using BlockPtr      = std::shared_ptr<Block>;
class Function;
class Registers;
class Upvalues;
class TableLiteral;

struct Declaration {
    std::string name;
    int begin = 0;
    int end = 0;
    int reg = -1;
    bool forLoop = false;
    bool forLoopExplicit = false;
};
using DeclarationPtr = std::shared_ptr<Declaration>;

template<typename T>
class Stack {
public:
    bool empty() const { return data_.empty(); }
    T& peek() { return data_.back(); }
    const T& peek() const { return data_.back(); }
    void push(const T& v) { data_.push_back(v); }
    T pop() { T v = std::move(data_.back()); data_.pop_back(); return v; }
    void reverse() { std::reverse(data_.begin(), data_.end()); }
    size_t size() const { return data_.size(); }
    typename std::vector<T>::iterator begin() { return data_.begin(); }
    typename std::vector<T>::iterator end() { return data_.end(); }
private:
    std::vector<T> data_;
};

class Output {
public:
    std::ostringstream os;
    int indent_level = 0;
    int position = 0;
    void start() {
        if (position == 0) {
            for (int i = 0; i < indent_level; i++) os << ' ';
            position = indent_level;
        }
    }
    void print(const std::string& s) { start(); os << s; position += (int)s.size(); }
    void print(const char* s) { start(); os << s; position += (int)std::strlen(s); }
    void print(char c) { start(); os.put(c); position++; }
    void println() { start(); os << '\n'; position = 0; }
    void println(const std::string& s) { print(s); println(); }
    void indent() { indent_level += 2; }
    void dedent() { indent_level -= 2; }
    std::string str() const { return os.str(); }
};

struct Constant {
    int type = 0;
    bool b = false;
    double n = 0.0;
    std::string s;

    Constant() = default;
    static Constant fromLObject(const LObject& o) {
        Constant c;
        switch (o.tag) {
            case LObject::NIL:    c.type = 0; break;
            case LObject::BOOL:   c.type = 1; c.b = o.b; break;
            case LObject::NUMBER: c.type = 2; c.n = o.n; break;
            case LObject::STRING: c.type = 3; c.s = o.s.value; break;
        }
        return c;
    }
    static Constant fromInt(int v) {
        Constant c; c.type = 2; c.n = (double)v; return c;
    }

    bool isNil()     const { return type == 0; }
    bool isBoolean() const { return type == 1; }
    bool isNumber()  const { return type == 2; }
    bool isString()  const { return type == 3; }
    bool isInteger() const {
        if (type != 2) return false;
        if (n != n) return false;
        return n == (double)(int64_t)n;
    }
    int asInteger()  const { return (int)(int64_t)n; }
    const std::string& asName() const { return s; }
    bool isIdentifier() const;
    void print(Output& out, bool braced) const;
};

static const std::unordered_set<std::string>& luaReservedWords() {
    static const std::unordered_set<std::string> kReserved = {
        "and","break","do","else","elseif","end","false","for","function","if",
        "in","local","nil","not","or","repeat","return","then","true","until","while",
        "goto"
    };
    return kReserved;
}

static bool isLuaIdentifierString(const std::string& s) {
    if (s.empty()) return false;
    if (luaReservedWords().count(s)) return false;
    char c = s[0];
    if (!(c == '_' || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'))) return false;
    for (size_t i = 1; i < s.size(); i++) {
        char ch = s[i];
        if (!(ch == '_' || (ch >= '0' && ch <= '9') ||
              (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z'))) return false;
    }
    return true;
}

bool Constant::isIdentifier() const {
    if (type != 3) return false;
    return isLuaIdentifierString(s);
}

void Constant::print(Output& out, bool  ) const {
    switch (type) {
        case 0: out.print("nil"); return;
        case 1: out.print(b ? "true" : "false"); return;
        case 2: {
            std::ostringstream os; os.precision(14);
            if (n != n) { out.print("(0/0)"); return; }
            if (n > 1e308) { out.print("(1/0)"); return; }
            if (n < -1e308) { out.print("(-1/0)"); return; }
            if (n == (double)(int64_t)n && n >= -1e15 && n <= 1e15) os << (int64_t)n;
            else os << n;
            out.print(os.str());
            return;
        }
        case 3: {
            std::string esc;
            esc.reserve(s.size() + 2);
            esc += '"';
            for (unsigned char ch : s) {
                switch (ch) {
                    case '"':  esc += "\\\""; break;
                    case '\\': esc += "\\\\"; break;
                    case '\n': esc += "\\n";  break;
                    case '\r': esc += "\\r";  break;
                    case '\t': esc += "\\t";  break;
                    case '\a': esc += "\\a";  break;
                    case '\b': esc += "\\b";  break;
                    case '\f': esc += "\\f";  break;
                    case '\v': esc += "\\v";  break;
                    default:
                        if (ch < 32 || ch >= 127) {
                            char buf[8];
                            std::snprintf(buf, sizeof(buf), "\\%d", (int)ch);
                            esc += buf;
                        } else {
                            esc += (char)ch;
                        }
                }
            }
            esc += '"';
            out.print(esc);
            return;
        }
    }
}

