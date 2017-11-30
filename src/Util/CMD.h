﻿/*
 * MIT License
 *
 * Copyright (c) 2016 xiongziliang <771730766@qq.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef SRC_UTIL_CMD_H_
#define SRC_UTIL_CMD_H_

#if defined(_WIN32)
#include "win32/getopt.h"
#else
#include <getopt.h>
#endif // defined(_WIN32)

#include <map>
#include <mutex>
#include <string>
#include <memory>
#include <vector>
#include <sstream>
#include <iostream>
#include <functional>
#include "Util/mini.h"
#include "Util/onceToken.h"

using namespace std;

namespace ZL{
namespace Util{

class Option {
public:
    typedef function<bool(const std::shared_ptr<ostream> &stream, const string &arg)> OptionHandler;
    enum ArgType {
        ArgNone = no_argument,
        ArgRequired = required_argument,
        ArgOptional = optional_argument
    };
    Option(){}
    Option(char shortOpt,
           const char *longOpt,
           enum ArgType argType,
           const char *defaultValue,
           bool mustExist,//该参数是否必须存在
           const char *des,
           const OptionHandler &cb) {
        _shortOpt = shortOpt;
        _longOpt = longOpt;
        _argType = argType;
        if(argType != ArgNone){
            if(defaultValue){
                _defaultValue.reset(new string(defaultValue));
            }
            if(!_defaultValue && mustExist){
                _mustExist = true;
            }
        }
        _des = des;
        _cb = cb;
    }
    virtual ~Option() {}
    bool operator()(const std::shared_ptr<ostream> &stream, const string &arg){
        return _cb ? _cb(stream,arg): true;
    }
private:
    friend class OptionParser;
    char _shortOpt;
    string _longOpt;
    std::shared_ptr<string> _defaultValue;
    enum ArgType _argType;
    string _des;
    OptionHandler _cb;
    bool _mustExist = false;
};

class OptionParser {
public:
    typedef function< void(const std::shared_ptr<ostream> &,mINI &)> OptionCompleted;
    OptionParser(const OptionCompleted &_cb = nullptr) {
        _onCompleted = _cb;
        _helper = Option('h', "help", Option::ArgNone, nullptr, false, "打印此信息",
                         [this](const std::shared_ptr<ostream> &stream,const string &arg)->bool {
            static const char *argsType[] = {"无参","有参","选参"};
            static const char *mustExist[] = {"选填","必填"};
            static string defaultPrefix = "默认:";
            static string defaultNull = "null";

            ostream &printer = *stream;
            int maxLen_longOpt = 0;
            int maxLen_default = defaultNull.size();

            for (auto &pr : _map_options) {
                auto &opt = pr.second;
                if(opt._longOpt.size() > maxLen_longOpt){
                    maxLen_longOpt = opt._longOpt.size();
                }
                if(opt._defaultValue){
                    if(opt._defaultValue->size() > maxLen_default){
                        maxLen_default = opt._defaultValue->size();
                    }
                }
            }
            for (auto &pr : _map_options) {
                auto &opt = pr.second;
                //打印短参和长参名
                if(opt._shortOpt){
                    printer <<"  -" << opt._shortOpt <<"  --" << opt._longOpt;
                }else{
                    printer <<"   " << " " <<"  --" << opt._longOpt;
                }
                for(int i=0;i< maxLen_longOpt - opt._longOpt.size();++i){
                    printer << " ";
                }
                //打印是否有参
                printer << "  " << argsType[opt._argType];
                //打印默认参数
                string defaultValue = defaultNull;
                if(opt._defaultValue){
                    defaultValue = *opt._defaultValue;
                }
                printer << "  " << defaultPrefix << defaultValue;
                for(int i=0;i< maxLen_default - defaultValue.size();++i){
                    printer << " ";
                }
                //打印是否必填参数
                printer << "  " << mustExist[opt._mustExist];
                //打印描述
                printer << "  " << opt._des << endl;
            }
            throw std::invalid_argument("");
        });
        (*this) << _helper;
    }
    virtual ~OptionParser() {
    }

    OptionParser &operator <<(Option &&option) {
        int index = 0xFF + _map_options.size();
        if(option._shortOpt){
            _map_charIndex.emplace(option._shortOpt,index);
        }
        _map_options.emplace(index, std::forward<Option>(option));
        return *this;
    }
    OptionParser &operator <<(const Option &option) {
        int index = 0xFF + _map_options.size();
        if(option._shortOpt){
            _map_charIndex.emplace(option._shortOpt,index);
        }
        _map_options.emplace(index, option);
        return *this;
    }
    void delOption(const char *key){
        for (auto &pr : _map_options) {
            if(pr.second._longOpt == key){
                if(pr.second._shortOpt){
                    _map_charIndex.erase(pr.second._shortOpt);
                }
                _map_options.erase(pr.first);
                break;
            }
        }
    }
    void operator ()(mINI &allArg, int argc, char *argv[],const std::shared_ptr<ostream> &stream) {
        (*this) << endl;
        lock_guard<mutex> lck(s_mtx_opt);
        int index;
        optind = 0;
        opterr = 0;
        while ((index = getopt_long(argc, argv, &_str_shortOpt[0], &_vec_longOpt[0],NULL)) != -1) {
            stringstream ss;
            ss  << "  未识别的选项,输入\"-h\"获取帮助.";
            if(index < 0xFF){
                //短参数
                auto it = _map_charIndex.find(index);
                if(it == _map_charIndex.end()){
                    throw std::invalid_argument(ss.str());
                }
                index = it->second;
            }

            auto it = _map_options.find(index);
            if(it == _map_options.end()){
                throw std::invalid_argument(ss.str());
            }
            auto &opt = it->second;
            auto pr = allArg.emplace(opt._longOpt, optarg ? optarg : "");
            if (!opt(stream, pr.first->second)) {
                return;
            }
            optarg = NULL;
        }
        for (auto &pr : _map_options) {
            if(pr.second._defaultValue && allArg.find(pr.second._longOpt) == allArg.end()){
                //有默认值,赋值默认值
                allArg.emplace(pr.second._longOpt,*pr.second._defaultValue);
            }
        }
        for (auto &pr : _map_options) {
            if(pr.second._mustExist){
                if(allArg.find(pr.second._longOpt) == allArg.end() ){
                    stringstream ss;
                    ss << "  参数\"" << pr.second._longOpt << "\"必须提供,输入\"-h\"选项获取帮助";
                    throw std::invalid_argument(ss.str());
                }
            }
        }
        if(allArg.empty() && _map_options.size() > 1){
            _helper(stream,"");
            return;
        }
        if (_onCompleted) {
            _onCompleted(stream, allArg);
        }
    }
private:
    void operator <<(ostream&(*f)(ostream&)) {
        _str_shortOpt.clear();
        _vec_longOpt.clear();
        struct option tmp;
        for (auto &pr : _map_options) {
            auto &opt = pr.second;
            //long opt
            tmp.name = (char *)opt._longOpt.data();
            tmp.has_arg = opt._argType;
            tmp.flag = NULL;
            tmp.val = pr.first;
            _vec_longOpt.emplace_back(tmp);
            //short opt
            if(!opt._shortOpt){
                continue;
            }
            _str_shortOpt.push_back(opt._shortOpt);
            switch (opt._argType) {
                case Option::ArgRequired:
                    _str_shortOpt.append(":");
                    break;
                case Option::ArgOptional:
                    _str_shortOpt.append("::");
                    break;
                default:
                    break;
            }
        }
        tmp.flag=0;
        tmp.name=0;
        tmp.has_arg=0;
        tmp.val=0;
        _vec_longOpt.emplace_back(tmp);
    }
private:
    map<int,Option> _map_options;
    map<char,int> _map_charIndex;
    OptionCompleted _onCompleted;
    vector<struct option> _vec_longOpt;
    string _str_shortOpt;
    Option _helper;
    static mutex s_mtx_opt;
};

class CMD :public mINI{
public:
    CMD(){};
    virtual ~CMD(){};
    virtual const char *description() const {
        return "description";
    }
    void operator ()(int argc, char *argv[],const std::shared_ptr<ostream> &stream = nullptr) {
        this->clear();
        std::shared_ptr<ostream> coutPtr(&cout,[](ostream *){});
        (*_parser)(*this,argc, argv,stream? stream : coutPtr );
    }

    bool hasKey(const char *key){
        return this->find(key) != this->end();
    }

    vector<variant> splitedVal(const char *key,const char *delim= ":"){
        vector<variant> ret;
        auto &val = (*this)[key];
        split(val,delim,ret);
        return ret;
    }
    void delOption(const char *key){
        if(_parser){
            _parser->delOption(key);
        }
    }
protected:
    std::shared_ptr<OptionParser> _parser;
private:
    //注意：当字符串为空时，也会返回一个空字符串
    void split(const string& s, const char *delim, vector<variant> &ret) {
        size_t last = 0;
        size_t index = s.find_first_of(delim, last);
        while (index != string::npos) {
            ret.push_back(s.substr(last, index - last));
            last = index + 1;
            index = s.find_first_of(delim, last);
        }
        if (index - last > 0) {
            ret.push_back(s.substr(last, index - last));
        }
    }
};


class CMDRegister
{
public:
    CMDRegister() {};
    virtual ~CMDRegister(){};
    static CMDRegister &Instance(){
        static CMDRegister instance;
        return instance;
    }
    void clear(){
        lock_guard<recursive_mutex> lck(_mtxCMD);
        _mapCMD.clear();
    }
    void registCMD(const char *name,const std::shared_ptr<CMD> &cmd){
        lock_guard<recursive_mutex> lck(_mtxCMD);
        _mapCMD.emplace(name,cmd);
    }
    void unregistCMD(const char *name){
        lock_guard<recursive_mutex> lck(_mtxCMD);
        _mapCMD.erase(name);
    }
    std::shared_ptr<CMD> operator[](const char *name){
        lock_guard<recursive_mutex> lck(_mtxCMD);
        auto it = _mapCMD.find(name);
        if(it == _mapCMD.end()){
            throw std::invalid_argument(string("命令不存在:") + name);
        }
        return it->second;
    }

    void operator()(const char *name,int argc,char *argv[],const std::shared_ptr<ostream> &stream = nullptr){
        auto cmd = (*this)[name];
        if(!cmd){
            throw std::invalid_argument(string("命令不存在:") + name);
        }
        (*cmd)(argc,argv,stream);
    };
    void printHelp(const std::shared_ptr<ostream> &streamTmp = nullptr){
        auto stream = streamTmp;
        if(!stream){
            stream.reset(&cout,[](ostream *){});
        }

        lock_guard<recursive_mutex> lck(_mtxCMD);
        int maxLen = 0;
        for (auto &pr : _mapCMD) {
            if(pr.first.size() > maxLen){
                maxLen = pr.first.size();
            }
        }
        for (auto &pr : _mapCMD) {
            (*stream) << "  " << pr.first;
            for(int i=0;i< maxLen - pr.first.size();++i){
                (*stream) << " ";
            }
            (*stream) << "  " << pr.second->description() << endl;
        }
    };
    void operator()(const string &line,const std::shared_ptr<ostream> &stream = nullptr){
        if(line.empty()){
            return;
        }
        vector<char *> argv;
        int argc = getArgs((char *)line.data(), argv);
        if (argc == 0) {
            return;
        }
        string cmd = argv[0];
        lock_guard<recursive_mutex> lck(_mtxCMD);
        auto it = _mapCMD.find(cmd);
        if (it == _mapCMD.end()) {
            stringstream ss;
            ss << "  未识别的命令\"" << cmd << "\",输入 \"help\" 获取帮助.";
            throw std::invalid_argument(ss.str());
        }
        (*it->second)(argc,&argv[0],stream);
    };
private:
    int getArgs(char *buf, vector<char *> &argv) {
        int argc = 0;
        bool start = false;
        int len = strlen(buf);
        for (int i = 0; i < len; i++) {
            if (buf[i] != ' ' && buf[i] != '\t' && buf[i] != '\r' && buf[i] != '\n') {
                if (!start) {
                    start = true;
                    if(argv.size() < argc + 1){
                        argv.resize(argc + 1);
                    }
                    argv[argc++] = buf + i;
                }
            } else {
                buf[i] = '\0';
                start = false;
            }
        }
        return argc;
    }
private:
    map<string,std::shared_ptr<CMD> > _mapCMD;
    recursive_mutex _mtxCMD;

};

//帮助命令(help)，该命令默认已注册
class CMD_help: public CMD {
public:
    CMD_help(){
        _parser.reset( new OptionParser(nullptr));
        (*_parser) << Option('c', "cmd", Option::ArgNone, nullptr ,false,"列出所有命令",
                             [](const std::shared_ptr<ostream> &stream,const string &arg) {
            CMDRegister::Instance().printHelp(stream);
            return true;
        });
    }
    virtual ~CMD_help() {}

    const char *description() const override {
        return "打印帮助信息";
    }
};

class ExitException : public std::exception
{
public:
    ExitException(){}
    virtual ~ExitException(){}

};

//退出程序命令(exit)，该命令默认已注册
class CMD_exit: public CMD {
public:
    CMD_exit(){
        _parser.reset( new OptionParser([](const std::shared_ptr<ostream> &,mINI &){
            throw ExitException();
        }));
    }
    virtual ~CMD_exit() {}

    const char *description() const override {
        return "退出shell";
    }
};

//退出程序命令(quit),该命令默认已注册
#define CMD_quit CMD_exit

//清空屏幕信息命令(clear)，该命令默认已注册
class CMD_clear : public CMD
{
public:
    CMD_clear(){
        _parser.reset(new OptionParser([this](const std::shared_ptr<ostream> &stream,mINI &args){
            clear(stream);
        }));
    }
    virtual ~CMD_clear(){}
    const char *description() const {
        return "清空屏幕输出";
    }
private:
    void clear(const std::shared_ptr<ostream> &stream){
        (*stream) << "\x1b[2J\x1b[H";
        stream->flush();
    }
};

}//namespace Util
}//namespace ZL

#define GET_CMD(name) (*(CMDRegister::Instance()[name]))
#define CMD_DO(name,...) (*(CMDRegister::Instance()[name]))(__VA_ARGS__)
#define REGIST_CMD(name) CMDRegister::Instance().registCMD(#name,std::make_shared<CMD_##name>());


#endif /* SRC_UTIL_CMD_H_ */