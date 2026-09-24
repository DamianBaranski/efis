/// \file polish_speech.cpp
/// C++ port of the PyTDM transliteration (not its speech engine).
///
/// Pytońska treść do mowy, Copyright (c) 2020 test0wanie, MIT License.
/// https://github.com/ggegoge/PyTDM
/// Permission is granted to use, copy, modify, and distribute this port
/// with the copyright notice and this permission notice preserved.
/// The software is provided as is, without warranty.
#include "polish_speech.h"

#include <functional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace polish_detail
{
struct RuleSpec
{
    const char *pattern;
    const char *repl;
};

#include "polish_rules.inc"

enum class Op
{
    Empty,
    Lit,
    Any,
    Cls,
    Edge,
    Cap,
    Alt,
    Cat,
    Rep,
};

struct Node
{
    Op op = Op::Empty;
    char32_t lit = 0;
    bool neg = false;
    bool space = false;
    bool word = false;
    bool digit = false;
    bool boundary = false;
    int cap = 0;
    int rmin = 0;
    int rmax = 0;
    std::u32string set;
    std::vector<Node> kids;
};

struct Rule
{
    Node root;
    std::u32string repl;
    int groups = 0;
};

std::u32string decode(const std::string &in)
{
    std::u32string out;
    for (size_t i = 0; i < in.size();)
    {
        const auto c = static_cast<unsigned char>(in[i]);
        if (c < 0x80)
        {
            out.push_back(c);
            ++i;
        }
        else if ((c & 0xE0) == 0xC0 && i + 1 < in.size())
        {
            out.push_back((static_cast<char32_t>(c & 0x1F) << 6) | (static_cast<unsigned char>(in[i + 1]) & 0x3F));
            i += 2;
        }
        else if ((c & 0xF0) == 0xE0 && i + 2 < in.size())
        {
            out.push_back((static_cast<char32_t>(c & 0x0F) << 12) |
                          (static_cast<char32_t>(static_cast<unsigned char>(in[i + 1]) & 0x3F) << 6) |
                          (static_cast<unsigned char>(in[i + 2]) & 0x3F));
            i += 3;
        }
        else if ((c & 0xF8) == 0xF0 && i + 3 < in.size())
        {
            out.push_back((static_cast<char32_t>(c & 0x07) << 18) |
                          (static_cast<char32_t>(static_cast<unsigned char>(in[i + 1]) & 0x3F) << 12) |
                          (static_cast<char32_t>(static_cast<unsigned char>(in[i + 2]) & 0x3F) << 6) |
                          (static_cast<unsigned char>(in[i + 3]) & 0x3F));
            i += 4;
        }
        else
        {
            out.push_back(c);
            ++i;
        }
    }
    return out;
}

std::string encode(const std::u32string &in)
{
    std::string out;
    for (char32_t c : in)
    {
        if (c < 0x80)
        {
            out.push_back(static_cast<char>(c));
        }
        else if (c < 0x800)
        {
            out.push_back(static_cast<char>(0xC0 | (c >> 6)));
            out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        }
        else if (c < 0x10000)
        {
            out.push_back(static_cast<char>(0xE0 | (c >> 12)));
            out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        }
        else
        {
            out.push_back(static_cast<char>(0xF0 | (c >> 18)));
            out.push_back(static_cast<char>(0x80 | ((c >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        }
    }
    return out;
}

char32_t lowerPl(char32_t c)
{
    if (c >= U'A' && c <= U'Z')
    {
        return c - U'A' + U'a';
    }
    switch (c)
    {
    case U'Ą': return U'ą';
    case U'Ć': return U'ć';
    case U'Ę': return U'ę';
    case U'Ł': return U'ł';
    case U'Ń': return U'ń';
    case U'Ó': return U'ó';
    case U'Ś': return U'ś';
    case U'Ź': return U'ź';
    case U'Ż': return U'ż';
    case U'Á': return U'á';
    case U'Č': return U'č';
    case U'Ď': return U'ď';
    case U'É': return U'é';
    case U'Ě': return U'ě';
    case U'Í': return U'í';
    case U'Ň': return U'ň';
    case U'Ř': return U'ř';
    case U'Š': return U'š';
    case U'Ť': return U'ť';
    case U'Ú': return U'ú';
    case U'Ů': return U'ů';
    case U'Ý': return U'ý';
    case U'Ž': return U'ž';
    default: return c;
    }
}

bool isSpace(char32_t c)
{
    return c == U' ' || c == U'\t' || c == U'\n' || c == U'\r' || c == U'\f' || c == U'\v' || c == 0xA0;
}

bool isDigit(char32_t c)
{
    return c >= U'0' && c <= U'9';
}

bool isWord(char32_t c)
{
    if (c == U'_' || isDigit(c))
    {
        return true;
    }
    if ((c >= U'A' && c <= U'Z') || (c >= U'a' && c <= U'z'))
    {
        return true;
    }
    return (c >= 0x00C0 && c <= 0x024F) || (c >= 0x1E00 && c <= 0x1EFF);
}

bool isPolishLetter(char32_t c)
{
    switch (c)
    {
    case U'ą':
    case U'ć':
    case U'ę':
    case U'ł':
    case U'ń':
    case U'ó':
    case U'ś':
    case U'ź':
    case U'ż':
        return true;
    default:
        return false;
    }
}

bool isCzechLetter(char32_t c)
{
    switch (c)
    {
    case U'á':
    case U'č':
    case U'ď':
    case U'é':
    case U'ě':
    case U'í':
    case U'ň':
    case U'ř':
    case U'š':
    case U'ť':
    case U'ú':
    case U'ů':
    case U'ý':
    case U'ž':
        return true;
    default:
        return false;
    }
}

struct Parser
{
    std::u32string pat;
    size_t i = 0;
    int caps = 0;

    bool more() const { return i < pat.size(); }
    char32_t peek() const { return pat[i]; }

    char32_t get()
    {
        if (!more())
        {
            throw std::runtime_error("polish pattern ended early");
        }
        return pat[i++];
    }

    Node parseAlt();
    Node parseCat();
    Node parseRep();
    Node parseAtom();
};

Node Parser::parseAlt()
{
    std::vector<Node> kids;
    kids.push_back(parseCat());
    while (more() && peek() == U'|')
    {
        get();
        kids.push_back(parseCat());
    }
    if (kids.size() == 1)
    {
        return std::move(kids[0]);
    }
    Node alt;
    alt.op = Op::Alt;
    alt.kids = std::move(kids);
    return alt;
}

Node Parser::parseCat()
{
    std::vector<Node> kids;
    while (more() && peek() != U'|' && peek() != U')')
    {
        kids.push_back(parseRep());
    }
    if (kids.empty())
    {
        return Node{};
    }
    if (kids.size() == 1)
    {
        return std::move(kids[0]);
    }
    Node cat;
    cat.op = Op::Cat;
    cat.kids = std::move(kids);
    return cat;
}

Node makeRep(Node child, int rmin, int rmax)
{
    Node rep;
    rep.op = Op::Rep;
    rep.rmin = rmin;
    rep.rmax = rmax;
    rep.kids.push_back(std::move(child));
    return rep;
}

Node Parser::parseRep()
{
    Node atom = parseAtom();
    if (!more())
    {
        return atom;
    }
    if (peek() == U'+')
    {
        get();
        return makeRep(std::move(atom), 1, -1);
    }
    if (peek() == U'*')
    {
        get();
        return makeRep(std::move(atom), 0, -1);
    }
    if (peek() == U'?')
    {
        get();
        return makeRep(std::move(atom), 0, 1);
    }
    if (peek() != U'{')
    {
        return atom;
    }
    get();
    int rmin = 0;
    while (more() && isDigit(peek()))
    {
        rmin = rmin * 10 + static_cast<int>(get() - U'0');
    }
    int rmax = rmin;
    if (more() && peek() == U',')
    {
        get();
        if (more() && peek() == U'}')
        {
            rmax = -1;
        }
        else
        {
            rmax = 0;
            while (more() && isDigit(peek()))
            {
                rmax = rmax * 10 + static_cast<int>(get() - U'0');
            }
        }
    }
    if (!more() || get() != U'}')
    {
        throw std::runtime_error("polish pattern has a broken quantifier");
    }
    return makeRep(std::move(atom), rmin, rmax);
}

Node classOf(bool space, bool word, bool digit, bool neg)
{
    Node node;
    node.op = Op::Cls;
    node.space = space;
    node.word = word;
    node.digit = digit;
    node.neg = neg;
    return node;
}

Node Parser::parseAtom()
{
    const char32_t c = get();
    if (c == U'(')
    {
        const int id = ++caps;
        Node inner = parseAlt();
        if (!more() || get() != U')')
        {
            throw std::runtime_error("polish pattern is missing ')'");
        }
        Node cap;
        cap.op = Op::Cap;
        cap.cap = id;
        cap.kids.push_back(std::move(inner));
        return cap;
    }
    if (c == U'[')
    {
        Node node;
        node.op = Op::Cls;
        if (more() && peek() == U'^')
        {
            node.neg = true;
            get();
        }
        while (more() && peek() != U']')
        {
            char32_t item = get();
            if (item == U'\\')
            {
                const char32_t esc = get();
                if (esc == U's')
                {
                    node.space = true;
                }
                else if (esc == U'w')
                {
                    node.word = true;
                }
                else if (esc == U'd')
                {
                    node.digit = true;
                }
                else
                {
                    node.set.push_back(esc);
                }
            }
            else
            {
                node.set.push_back(item);
            }
        }
        if (!more() || get() != U']')
        {
            throw std::runtime_error("polish pattern is missing ']'");
        }
        return node;
    }
    if (c == U'.')
    {
        Node node;
        node.op = Op::Any;
        return node;
    }
    if (c == U'\\')
    {
        const char32_t esc = get();
        if (esc == U'b' || esc == U'B')
        {
            Node node;
            node.op = Op::Edge;
            node.boundary = esc == U'b';
            return node;
        }
        if (esc == U'w')
        {
            return classOf(false, true, false, false);
        }
        if (esc == U'W')
        {
            return classOf(false, true, false, true);
        }
        if (esc == U's')
        {
            return classOf(true, false, false, false);
        }
        if (esc == U'S')
        {
            return classOf(true, false, false, true);
        }
        if (esc == U'd')
        {
            return classOf(false, false, true, false);
        }
        if (esc == U'D')
        {
            return classOf(false, false, true, true);
        }
        Node lit;
        lit.op = Op::Lit;
        lit.lit = esc;
        return lit;
    }
    Node lit;
    lit.op = Op::Lit;
    lit.lit = c;
    return lit;
}

Rule compileRule(const RuleSpec &spec)
{
    Parser parser;
    parser.pat = decode(spec.pattern);
    Node root = parser.parseAlt();
    if (parser.more())
    {
        throw std::runtime_error(std::string("polish pattern was not fully read: ") + spec.pattern);
    }
    Rule rule;
    rule.root = std::move(root);
    rule.repl = decode(spec.repl);
    rule.groups = parser.caps;
    return rule;
}

const std::vector<Rule> &tableFor(const RuleSpec *spec, size_t count)
{
    static std::vector<std::vector<Rule>> cache;
    // Each table is compiled once. The pointer identity picks the cache slot.
    static std::vector<const RuleSpec *> keys;
    for (size_t n = 0; n < keys.size(); ++n)
    {
        if (keys[n] == spec)
        {
            return cache[n];
        }
    }
    std::vector<Rule> built;
    built.reserve(count);
    for (size_t n = 0; n < count; ++n)
    {
        built.push_back(compileRule(spec[n]));
    }
    keys.push_back(spec);
    cache.push_back(std::move(built));
    return cache.back();
}

class Matcher
{
public:
    bool prefix(const Rule &rule, const std::u32string &text, std::vector<std::pair<int, int>> &groups)
    {
        s = &text;
        caps.assign(static_cast<size_t>(rule.groups) + 1, {-1, -1});
        const bool ok = match(rule.root, 0, [&](size_t end) {
            caps[0] = {0, static_cast<int>(end)};
            return true;
        });
        groups = caps;
        return ok;
    }

    std::u32string replace(const std::u32string &text, const Rule &rule)
    {
        s = &text;
        std::u32string out;
        size_t i = 0;
        while (i <= text.size())
        {
            caps.assign(static_cast<size_t>(rule.groups) + 1, {-1, -1});
            size_t end = i;
            const bool ok = match(rule.root, i, [&](size_t j) {
                end = j;
                caps[0] = {static_cast<int>(i), static_cast<int>(j)};
                return true;
            });
            if (ok && end > i)
            {
                out += expand(rule.repl, text);
                i = end;
                continue;
            }
            if (ok)
            {
                out += expand(rule.repl, text);
            }
            if (i >= text.size())
            {
                break;
            }
            out.push_back(text[i]);
            ++i;
        }
        return out;
    }

private:
    const std::u32string *s = nullptr;
    std::vector<std::pair<int, int>> caps;

    bool boundaryAt(size_t i) const
    {
        const bool left = i > 0 && isWord((*s)[i - 1]);
        const bool right = i < s->size() && isWord((*s)[i]);
        return left != right;
    }

    bool inClass(const Node &node, char32_t c) const
    {
        bool hit = false;
        if (node.space && isSpace(c))
        {
            hit = true;
        }
        if (node.word && isWord(c))
        {
            hit = true;
        }
        if (node.digit && isDigit(c))
        {
            hit = true;
        }
        if (node.set.find(c) != std::u32string::npos)
        {
            hit = true;
        }
        return node.neg ? !hit : hit;
    }

    std::u32string expand(const std::u32string &repl, const std::u32string &text) const
    {
        std::u32string out;
        for (size_t n = 0; n < repl.size(); ++n)
        {
            if (repl[n] == U'\\' && n + 1 < repl.size() && repl[n + 1] >= U'0' && repl[n + 1] <= U'9')
            {
                const size_t group = static_cast<size_t>(repl[++n] - U'0');
                if (group < caps.size() && caps[group].first >= 0)
                {
                    const int from = caps[group].first;
                    const int to = caps[group].second;
                    out.append(text, static_cast<size_t>(from), static_cast<size_t>(to - from));
                }
            }
            else
            {
                out.push_back(repl[n]);
            }
        }
        return out;
    }

    bool match(const Node &node, size_t i, const std::function<bool(size_t)> &cont);
    bool matchCat(const std::vector<Node> &kids, size_t k, size_t i, const std::function<bool(size_t)> &cont);
    bool matchRep(const Node &node, size_t i, int count, const std::function<bool(size_t)> &cont);
};

bool Matcher::matchCat(const std::vector<Node> &kids, size_t k, size_t i, const std::function<bool(size_t)> &cont)
{
    if (k == kids.size())
    {
        return cont(i);
    }
    return match(kids[k], i, [&](size_t j) { return matchCat(kids, k + 1, j, cont); });
}

bool Matcher::matchRep(const Node &node, size_t i, int count, const std::function<bool(size_t)> &cont)
{
    if ((node.rmax < 0 || count < node.rmax) && count < 4096)
    {
        const bool more = match(node.kids[0], i, [&](size_t j) {
            if (j == i)
            {
                return false;
            }
            return matchRep(node, j, count + 1, cont);
        });
        if (more)
        {
            return true;
        }
    }
    if (count >= node.rmin)
    {
        return cont(i);
    }
    return false;
}

bool Matcher::match(const Node &node, size_t i, const std::function<bool(size_t)> &cont)
{
    switch (node.op)
    {
    case Op::Empty:
        return cont(i);
    case Op::Lit:
        if (i < s->size() && (*s)[i] == node.lit)
        {
            return cont(i + 1);
        }
        return false;
    case Op::Any:
        if (i < s->size() && (*s)[i] != U'\n')
        {
            return cont(i + 1);
        }
        return false;
    case Op::Cls:
        if (i < s->size() && inClass(node, (*s)[i]))
        {
            return cont(i + 1);
        }
        return false;
    case Op::Edge:
        if (boundaryAt(i) == node.boundary)
        {
            return cont(i);
        }
        return false;
    case Op::Cap:
        return match(node.kids[0], i, [&](size_t j) {
            const auto saved = caps[static_cast<size_t>(node.cap)];
            caps[static_cast<size_t>(node.cap)] = {static_cast<int>(i), static_cast<int>(j)};
            const bool ok = cont(j);
            if (!ok)
            {
                caps[static_cast<size_t>(node.cap)] = saved;
            }
            return ok;
        });
    case Op::Alt:
        for (const Node &kid : node.kids)
        {
            if (match(kid, i, cont))
            {
                return true;
            }
        }
        return false;
    case Op::Cat:
        return matchCat(node.kids, 0, i, cont);
    case Op::Rep:
        return matchRep(node, i, 0, cont);
    }
    return false;
}

std::u32string applyTable(std::u32string text, const RuleSpec *spec, size_t count)
{
    const std::vector<Rule> &rules = tableFor(spec, count);
    for (const Rule &rule : rules)
    {
        text = Matcher{}.replace(text, rule);
    }
    return text;
}

const char *kSub10[] = {"serraw", "yeah denn", "dvah", "tshi", "chtehrri", "pienntch", "sheshtch", "shehdehm", "oshehm",
                        "jevvienntch"};
const char *kTeen[] = {"jehshienntch",     "yeah dennashtch yeah", "dvahnashtch yeah",    "tshinashtch yeah",
                       "chternashtch yeah", "piehttnashtch yeah",   "shessnashtch yeah",   "shehdehmnashtch yeah",
                       "oshehmnashtch yeah", "jevvienntnashtch yeah"};
const char *kDec[] = {"", "", "dvahjehshtchiah", "tshijehshtchee", "chterjehshtchee", "piennjehshawnt", "sheshjehshawnt",
                      "shehdehmjehshawnt", "oshehmjehshawnt", "jevviehnjehshawnt"};

std::u32string numberSpeech(int n)
{
    if (n < -199 || n > 199)
    {
        return decode(std::to_string(n));
    }
    if (n < 0)
    {
        return decode("meenoos ") + numberSpeech(-n);
    }
    if (n < 10)
    {
        return decode(kSub10[n]);
    }
    if (n < 20)
    {
        return decode(kTeen[n - 10]);
    }
    if (n <= 100)
    {
        if (n == 100)
        {
            return decode("staw");
        }
        if (n % 10 == 0)
        {
            return decode(kDec[n / 10]);
        }
        return decode(kDec[n / 10]) + U" " + decode(kSub10[n % 10]);
    }
    const std::u32string rest = numberSpeech(n % 100);
    if (rest.empty())
    {
        return decode("staw");
    }
    return decode("staw ") + rest;
}

int parseInt(const std::u32string &text)
{
    if (text.empty())
    {
        return 0;
    }
    size_t i = 0;
    int sign = 1;
    if (text[0] == U'-')
    {
        sign = -1;
        i = 1;
    }
    int value = 0;
    for (; i < text.size(); ++i)
    {
        if (!isDigit(text[i]))
        {
            break;
        }
        value = value * 10 + static_cast<int>(text[i] - U'0');
    }
    return sign * value;
}

std::u32string slice(const std::u32string &text, std::pair<int, int> span)
{
    if (span.first < 0 || span.second < span.first)
    {
        return {};
    }
    return text.substr(static_cast<size_t>(span.first), static_cast<size_t>(span.second - span.first));
}

std::u32string nonDigits(const std::u32string &text)
{
    std::u32string out;
    for (char32_t c : text)
    {
        if (!isDigit(c))
        {
            out.push_back(c);
        }
    }
    return out;
}

std::u32string anglicizeText(const std::u32string &text);

std::u32string anglicizeWord(const std::u32string &word)
{
    std::vector<std::pair<int, int>> groups;
    const Rule &lead = tableFor(kLeadNumber, 1).front();
    if (Matcher{}.prefix(lead, word, groups))
    {
        return numberSpeech(parseInt(slice(word, groups[0]))) + nonDigits(word);
    }
    const Rule &tail = tableFor(kTailNumber, 1).front();
    if (Matcher{}.prefix(tail, word, groups) && groups.size() > 2)
    {
        return anglicizeText(slice(word, groups[1])) + U" " + numberSpeech(parseInt(slice(word, groups[2])));
    }
    const Rule &one = tableFor(kOneNumber, 1).front();
    if (Matcher{}.prefix(one, word, groups) && groups.size() > 2)
    {
        return numberSpeech(parseInt(slice(word, groups[2])));
    }
    return applyTable(word, kAngl, sizeof kAngl / sizeof kAngl[0]);
}

std::u32string anglicizeText(const std::u32string &text)
{
    std::u32string out;
    size_t i = 0;
    while (i < text.size())
    {
        while (i < text.size() && isSpace(text[i]))
        {
            ++i;
        }
        if (i >= text.size())
        {
            break;
        }
        const size_t start = i;
        while (i < text.size() && !isSpace(text[i]))
        {
            ++i;
        }
        if (!out.empty())
        {
            out.push_back(U' ');
        }
        out += anglicizeWord(text.substr(start, i - start));
    }
    return out;
}

bool polishSpelling(const std::string &text)
{
    const std::u32string raw = decode(text);
    bool polish = false;
    bool czech = false;
    std::u32string lower;
    lower.reserve(raw.size());
    for (char32_t c : raw)
    {
        const char32_t folded = lowerPl(c);
        lower.push_back(folded);
        polish = polish || isPolishLetter(folded);
        czech = czech || isCzechLetter(folded);
    }
    if (czech)
    {
        return false;
    }
    if (polish)
    {
        return true;
    }
    for (size_t i = 0; i + 1 < lower.size(); ++i)
    {
        const bool sz = lower[i] == U's' && lower[i + 1] == U'z';
        const bool rz = lower[i] == U'r' && lower[i + 1] == U'z';
        if (sz || rz)
        {
            return true;
        }
    }
    return false;
}

std::string anglicizePolish(const std::string &text)
{
    std::u32string folded = decode(text);
    for (char32_t &c : folded)
    {
        c = lowerPl(c);
    }
    folded = applyTable(std::move(folded), kRepolon, sizeof kRepolon / sizeof kRepolon[0]);
    return encode(anglicizeText(folded));
}
}

bool polishSpelling(const std::string &text)
{
    return polish_detail::polishSpelling(text);
}

std::string anglicizePolish(const std::string &text)
{
    return polish_detail::anglicizePolish(text);
}

#ifdef POLISH_SPEECH_TEST
#include <iostream>
int main()
{
    std::string line;
    while (std::getline(std::cin, line))
    {
        std::cout << anglicizePolish(line) << '\n';
    }
    return 0;
}
#endif
