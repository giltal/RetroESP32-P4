/*
 * stella_compat_streams.h — Complete stream replacement for ESP32-P4
 *
 * Replaces ALL C++ stream headers (<iostream>, <sstream>, <iomanip>,
 * <fstream>, <ostream>) to avoid pulling in ~124KB of locale/stream
 * infrastructure that overflows DRAM.
 *
 * Provides in namespace 'stella':
 *   - ostream (base class with virtual dispatch)
 *   - ostringstream (snprintf-based string builder)
 *   - ifstream, ofstream, fstream (FILE*-based)
 *   - ios (openmode constants)
 *   - Manipulators: hex, dec, endl, setw, setfill
 *   - null_ostream (for cout/cerr replacement)
 */
#ifndef STELLA_COMPAT_STREAMS_H
#define STELLA_COMPAT_STREAMS_H

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>

namespace stella {

/* Forward */
class ostream;
typedef ostream& (*ostream_manip)(ostream&);

/* =================================================================
 * ostream — base class for stream output
 * ================================================================= */
class ostream {
public:
    ostream() : hex_mode_(false) {}
    virtual ~ostream() {}

    ostream& operator<<(const char* s) {
        if (s) do_write(s, strlen(s));
        return *this;
    }
    ostream& operator<<(const std::string& s) {
        do_write(s.c_str(), s.size());
        return *this;
    }
    ostream& operator<<(char c) { do_write(&c, 1); return *this; }
    ostream& operator<<(short v) {
        char b[24]; int n = snprintf(b, sizeof(b), "%d", (int)v);
        if (n > 0) { do_write(b, n); } return *this;
    }
    ostream& operator<<(unsigned short v) {
        char b[24]; int n;
        if (hex_mode_) { n = snprintf(b, sizeof(b), "%x", (unsigned)v); }
        else           { n = snprintf(b, sizeof(b), "%u", (unsigned)v); }
        if (n > 0) { do_write(b, n); } return *this;
    }
    ostream& operator<<(int v) {
        char b[24]; int n;
        if (hex_mode_) { n = snprintf(b, sizeof(b), "%x", (unsigned)v); }
        else           { n = snprintf(b, sizeof(b), "%d", v); }
        if (n > 0) { do_write(b, n); } return *this;
    }
    ostream& operator<<(unsigned v) {
        char b[24]; int n;
        if (hex_mode_) { n = snprintf(b, sizeof(b), "%x", v); }
        else           { n = snprintf(b, sizeof(b), "%u", v); }
        if (n > 0) { do_write(b, n); } return *this;
    }
    ostream& operator<<(long v) {
        char b[24]; int n = snprintf(b, sizeof(b), "%ld", v);
        if (n > 0) { do_write(b, n); } return *this;
    }
    ostream& operator<<(unsigned long v) {
        char b[24]; int n;
        if (hex_mode_) { n = snprintf(b, sizeof(b), "%lx", v); }
        else           { n = snprintf(b, sizeof(b), "%lu", v); }
        if (n > 0) { do_write(b, n); } return *this;
    }
    ostream& operator<<(float v) {
        char b[32]; int n = snprintf(b, sizeof(b), "%g", (double)v);
        if (n > 0) { do_write(b, n); } return *this;
    }
    ostream& operator<<(double v) {
        char b[32]; int n = snprintf(b, sizeof(b), "%g", v);
        if (n > 0) { do_write(b, n); } return *this;
    }
    ostream& operator<<(bool v) { do_write(v ? "1" : "0", 1); return *this; }
    void put(char c) { do_write(&c, 1); }
    ostream& operator<<(const void* p) {
        char b[24]; int n = snprintf(b, sizeof(b), "%p", p);
        if (n > 0) { do_write(b, n); } return *this;
    }

    /* Manipulator (function pointer) */
    ostream& operator<<(ostream_manip f) { return f(*this); }

    void set_hex(bool h) { hex_mode_ = h; }
    bool get_hex() const { return hex_mode_; }

protected:
    virtual void do_write(const char* /*s*/, size_t /*len*/) {}
    bool hex_mode_;
};

/* =================================================================
 * ostringstream — builds a string in a fixed buffer
 * ================================================================= */
class ostringstream : public ostream {
public:
    ostringstream() : pos_(0) { buf_[0] = '\0'; }

    std::string str() const { return std::string(buf_, pos_); }
    void str(const std::string& s) {
        size_t len = s.size();
        if (len >= sizeof(buf_)) len = sizeof(buf_) - 1;
        memcpy(buf_, s.c_str(), len);
        buf_[len] = '\0';
        pos_ = len;
    }

protected:
    void do_write(const char* s, size_t len) override {
        if (pos_ + len >= sizeof(buf_)) len = sizeof(buf_) - 1 - pos_;
        if (len > 0) {
            memcpy(buf_ + pos_, s, len);
            pos_ += len;
            buf_[pos_] = '\0';
        }
    }

private:
    char buf_[2048];
    size_t pos_;
};

/* =================================================================
 * istringstream — parses values from a string
 * ================================================================= */
class istringstream {
public:
    istringstream() : pos_(0), fail_(false) {}
    explicit istringstream(const std::string& s) : data_(s), pos_(0), fail_(false) {}

    void str(const std::string& s) { data_ = s; pos_ = 0; fail_ = false; }
    std::string str() const { return data_; }

    istringstream& operator>>(int& v) {
        skip_ws();
        if (pos_ >= data_.size()) { fail_ = true; return *this; }
        char* endp;
        long val = strtol(data_.c_str() + pos_, &endp, 10);
        if (endp == data_.c_str() + pos_) { fail_ = true; return *this; }
        v = (int)val;
        pos_ = (size_t)(endp - data_.c_str());
        return *this;
    }
    istringstream& operator>>(unsigned& v) {
        skip_ws();
        if (pos_ >= data_.size()) { fail_ = true; return *this; }
        char* endp;
        unsigned long val = strtoul(data_.c_str() + pos_, &endp, 10);
        if (endp == data_.c_str() + pos_) { fail_ = true; return *this; }
        v = (unsigned)val;
        pos_ = (size_t)(endp - data_.c_str());
        return *this;
    }
    istringstream& operator>>(char& c) {
        if (pos_ < data_.size()) { c = data_[pos_++]; }
        else { fail_ = true; }
        return *this;
    }
    istringstream& operator>>(std::string& s) {
        skip_ws();
        size_t start = pos_;
        while (pos_ < data_.size() && !isspace((unsigned char)data_[pos_])) pos_++;
        s = data_.substr(start, pos_ - start);
        if (start == pos_) fail_ = true;
        return *this;
    }

    operator bool() const { return !fail_; }
    bool operator!() const { return fail_; }
    bool good() const { return !fail_; }

private:
    void skip_ws() {
        while (pos_ < data_.size() && isspace((unsigned char)data_[pos_])) pos_++;
    }
    std::string data_;
    size_t pos_;
    bool fail_;
};

/* =================================================================
 * Manipulators
 * ================================================================= */
inline ostream& hex(ostream& os) { os.set_hex(true); return os; }
inline ostream& dec(ostream& os) { os.set_hex(false); return os; }
inline ostream& endl(ostream& os) { os << '\n'; return os; }

struct _setw_t { int w; };
struct _setfill_t { char c; };
inline _setw_t setw(int w) { return _setw_t{w}; }
inline _setfill_t setfill(char c) { return _setfill_t{c}; }
inline ostream& operator<<(ostream& os, _setw_t) { return os; }
inline ostream& operator<<(ostream& os, _setfill_t) { return os; }

/* =================================================================
 * ios — openmode constants
 * ================================================================= */
struct ios {
    typedef int openmode;
    static const openmode in     = 0x01;
    static const openmode out    = 0x02;
    static const openmode binary = 0x04;
    static const openmode app    = 0x08;

    typedef int seekdir;
    static const seekdir beg = SEEK_SET;
    static const seekdir cur = SEEK_CUR;
    static const seekdir end = SEEK_END;
};

/* =================================================================
 * istream — base class for input streams
 * ================================================================= */
class istream {
public:
    istream() : fail_(false) {}
    virtual ~istream() {}

    virtual istream& operator>>(int& v)          { (void)v; fail_ = true; return *this; }
    virtual istream& operator>>(unsigned& v)     { (void)v; fail_ = true; return *this; }
    virtual istream& operator>>(char& c)         { (void)c; fail_ = true; return *this; }
    virtual istream& operator>>(std::string& s)  { (void)s; fail_ = true; return *this; }

    virtual bool getline_c(char* buf, size_t n) { (void)buf; (void)n; return false; }
    virtual bool get(char& c) { (void)c; fail_ = true; return false; }
    virtual int peek() { return -1; }

    operator bool() const { return !fail_; }
    bool operator!() const { return fail_; }
    bool good() const { return !fail_; }

protected:
    bool fail_;
};

/* Free-function getline for istream */
inline bool getline(istream& is, std::string& str) {
    char buf[2048];
    if (!is.getline_c(buf, sizeof(buf))) { str.clear(); return false; }
    str = buf;
    return true;
}

/* =================================================================
 * ifstream — read-only, wraps C FILE*, inherits istream
 * ================================================================= */
class ifstream : public istream {
public:
    ifstream() : fp_(NULL) {}
    explicit ifstream(const char* fn, int mode = ios::in) : fp_(NULL) { open(fn, mode); }
    explicit ifstream(const std::string& fn, int mode = ios::in) : fp_(NULL) { open(fn.c_str(), mode); }
    ~ifstream() { close(); }

    void open(const char* fn, int mode = ios::in) {
        close();
        fp_ = fopen(fn, (mode & ios::binary) ? "rb" : "r");
        fail_ = (fp_ == NULL);
    }
    bool is_open() const { return fp_ != NULL; }
    void close() { if (fp_) { fclose(fp_); fp_ = NULL; } }

    ifstream& read(char* buf, size_t n) {
        if (fp_) fread(buf, 1, n, fp_);
        return *this;
    }

    istream& operator>>(int& v) override {
        if (!fp_) { fail_ = true; return *this; }
        if (fscanf(fp_, "%d", &v) != 1) fail_ = true;
        return *this;
    }
    istream& operator>>(unsigned& v) override {
        if (!fp_) { fail_ = true; return *this; }
        if (fscanf(fp_, "%u", &v) != 1) fail_ = true;
        return *this;
    }
    istream& operator>>(char& c) override {
        if (!fp_) { fail_ = true; return *this; }
        int ch = fgetc(fp_);
        if (ch == EOF) { fail_ = true; } else { c = (char)ch; }
        return *this;
    }
    istream& operator>>(std::string& s) override {
        if (!fp_) { fail_ = true; return *this; }
        char buf[256]; int ch; size_t i = 0;
        while ((ch = fgetc(fp_)) != EOF && isspace(ch)) {} /* skip ws */
        if (ch == EOF) { fail_ = true; s.clear(); return *this; }
        buf[i++] = (char)ch;
        while (i < sizeof(buf) - 1 && (ch = fgetc(fp_)) != EOF && !isspace(ch))
            buf[i++] = (char)ch;
        buf[i] = '\0';
        s = buf;
        return *this;
    }

    bool getline_c(char* buf, size_t n) override {
        if (!fp_ || !fgets(buf, (int)n, fp_)) { fail_ = true; return false; }
        size_t len = strlen(buf);
        if (len > 0 && buf[len - 1] == '\n') buf[len - 1] = '\0';
        return true;
    }

    bool get(char& c) override {
        if (!fp_) { fail_ = true; return false; }
        int ch = fgetc(fp_);
        if (ch == EOF) { fail_ = true; return false; }
        c = (char)ch;
        return true;
    }
    int peek() override {
        if (!fp_) return -1;
        int ch = fgetc(fp_);
        if (ch != EOF) ungetc(ch, fp_);
        return ch;
    }

    void seekg(long offset, int whence = SEEK_SET) {
        if (fp_) fseek(fp_, offset, whence);
    }
    long tellg() {
        return fp_ ? ftell(fp_) : 0;
    }

    operator bool() const { return fp_ != NULL && !feof(fp_) && !fail_; }
    bool operator!() const { return !static_cast<bool>(*this); }
    bool good() const { return fp_ != NULL && !feof(fp_) && !ferror(fp_) && !fail_; }

private:
    FILE* fp_;
    ifstream(const ifstream&);
    ifstream& operator=(const ifstream&);
};

/* =================================================================
 * ofstream — write-only, wraps C FILE*, inherits ostream
 * ================================================================= */
class ofstream : public ostream {
public:
    ofstream() : fp_(NULL) {}
    explicit ofstream(const char* fn, int mode = ios::out) : fp_(NULL) { open(fn, mode); }
    explicit ofstream(const std::string& fn, int mode = ios::out) : fp_(NULL) { open(fn.c_str(), mode); }
    ~ofstream() { close(); }

    void open(const char* fn, int mode = ios::out) {
        close();
        const char* m;
        if (mode & ios::app) m = (mode & ios::binary) ? "ab" : "a";
        else                 m = (mode & ios::binary) ? "wb" : "w";
        fp_ = fopen(fn, m);
    }
    bool is_open() const { return fp_ != NULL; }
    void close() { if (fp_) { fclose(fp_); fp_ = NULL; } }

    ofstream& write(const char* buf, size_t n) {
        if (fp_) fwrite(buf, 1, n, fp_);
        return *this;
    }

    operator bool() const { return fp_ != NULL; }

protected:
    void do_write(const char* s, size_t len) override {
        if (fp_) fwrite(s, 1, len, fp_);
    }

private:
    FILE* fp_;
    ofstream(const ofstream&);
    ofstream& operator=(const ofstream&);
};

/* =================================================================
 * fstream — read+write, wraps C FILE*
 * ================================================================= */
class fstream {
public:
    fstream() : fp_(NULL) {}
    fstream(const char* fn, int mode) : fp_(NULL) { open(fn, mode); }
    ~fstream() { close(); }

    void open(const char* fn, int mode) {
        close();
        const char* m;
        if ((mode & ios::in) && (mode & ios::out))
            m = (mode & ios::binary) ? "r+b" : "r+";
        else if (mode & ios::out)
            m = (mode & ios::app) ? "ab" : ((mode & ios::binary) ? "wb" : "w");
        else
            m = (mode & ios::binary) ? "rb" : "r";
        fp_ = fopen(fn, m);
    }
    bool is_open() const { return fp_ != NULL; }
    void close() { if (fp_) { fclose(fp_); fp_ = NULL; } }

    operator bool() const { return fp_ != NULL; }

private:
    FILE* fp_;
    fstream(const fstream&);
    fstream& operator=(const fstream&);
};

/* =================================================================
 * null_ostream — discards all output (for cout/cerr)
 * ================================================================= */
struct null_ostream {
    template<typename T>
    null_ostream& operator<<(const T&) { return *this; }
    null_ostream& operator<<(ostream_manip) { return *this; }
};

} /* namespace stella */

/* Global null cout/cerr */
static stella::null_ostream _stella_null_cout;
static stella::null_ostream _stella_null_cerr;

#define STELLA_COUT _stella_null_cout
#define STELLA_CERR _stella_null_cerr

#endif /* STELLA_COMPAT_STREAMS_H */
