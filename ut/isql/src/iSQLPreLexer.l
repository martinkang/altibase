/** 
 *  Copyright (c) 1999~2017, Altibase Corp. and/or its affiliates. All rights reserved.
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU Affero General Public License, version 3,
 *  as published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU Affero General Public License for more details.
 *
 *  You should have received a copy of the GNU Affero General Public License
 *  along with this program. If not, see <http://www.gnu.org/licenses/>.
 */
 
/***********************************************************************
 * $Id: iSQLPreLexer.l 82186 2018-02-05 05:17:56Z lswhh $
 **********************************************************************/

/* ======================================================
   NAME
    iSQLPreLexer.l

   DESCRIPTION
    isql���� ������ 3���� ���ɾ� �����ڰ� �ִ�.
    -----------------------------------------------------
    \n    : isql command. ex)set, show, @, ...
    ;\n   : sql command.  ex) select, update, alter, ...
    \n/\n : create prodecure/function stmt.
    -----------------------------------------------------
    �� ������ ���ɾ��� ���� �ܾ ���� ���ɾ� ���� �˼� �־�
    �ϳ��� ���ɾ �Ϸ�� ������ �Է��� �޾Ƶ鿩 ���ۿ� ������ ����
    ���ɾ �Ϸ�Ǹ� ���۸� iSQLParserparse�� �Է����� �Ѱ��ش�.

   PUBLIC FUNCTION(S)

   PRIVATE FUNCTION(S)

   NOTES

   MODIFIED   (MM/DD/YY)
 ====================================================== */

%option 8bit
%option caseless
%option nounput noyymore noyywrap
%option prefix="iSQLPreLexer"

%{
#include <idl.h>
#include <iSQLProgOption.h>
#include <iSQLCompiler.h>
#include <iSQLSpool.h>

#ifdef USE_READLINE
#include <histedit.h>

extern iSQLProgOption gProgOption;
extern EditLine *gEdo;
extern History  *gHist;
extern HistEvent gEvent;
#endif

//#define _ISQL_DEBUG

extern iSQLCompiler * gSQLCompiler;
extern iSQLSpool    * gSpool;
extern iSQLBufMgr   * gBufMgr;
extern idBool         g_glogin;
extern idBool         g_login;
extern idBool         g_inLoad;
extern idBool         g_inEdit;
extern idBool         g_isSIGINT; // fix BUG-19750
idBool                g_IsProc = ID_FALSE;
/* bug 18731 */
extern SInt           gLastReturnValue;
/* ============================================
 * IsEOF  : isql exit if end-of-file detect
 * IsPLN  : PLN�� ���� ȣ��� ��� �ι�° PLN�� iSQLPreLexer_input����
 *          attach�� newline�� ���� ȣ��� ����̹Ƿ�
 *          �̶����� �ƹ��ϵ� ���� �ʴ´�.
 * PLNCnt : ���� enter�� �Էµ� ��� �Էµ�
 *          ���ɾ�� ���õǰ� ������Ʈ�� ����Ѵ�.
 * ============================================ */
idBool IsEOF     = ID_FALSE;
idBool IsPLN     = ID_FALSE;
SInt   PLNCnt    = 0;

SInt iSQLPreLexer_input(SChar * buf, SInt max_size);

#undef  YY_INPUT
#define YY_INPUT(b, r, m) ( r = iSQLPreLexer_input((SChar *)b, m) )

#undef YY_BUF_SIZE
#define YY_BUF_SIZE (gBufMgr->m_MaxBuf)

/* ============================================
 * APD  : when command continue
 *        append yytext to buf
 * RTN  : when command complete
 *        APD + return success
 * RTN2 : same RTN
 *        when create procedure command complete
 * RTC  : when comment only
 *        APD + return comment
 * RTC2 : when comment + command
 *        APD + return comment2
 * PLN  : when newline detect
 *        if empty : print promt
 *        else     : APD + print line number
 * PLN2 : in file
 *        empty or not : APD
 * ICP  : when invalid command detect
 *        case1. invalid command + \n : APD + return failure
 *        case2. \n only              : return empty
 * ============================================ */
#define APD()  { IsPLN = ID_FALSE; PLNCnt = 0; gBufMgr->Append(yytext); }
#define RTN()  { IsPLN = ID_TRUE; gBufMgr->Append(yytext); \
                 return IDE_SUCCESS; }
#define RTN2() { IsPLN = ID_TRUE; gBufMgr->Append(yytext+1); \
                 return IDE_SUCCESS; }
#define RTC()  { IsPLN = ID_TRUE; gBufMgr->Append(yytext); \
                 return ISQL_COMMENT; }
#define RTC2() { IsPLN = ID_TRUE; gBufMgr->Append(yytext); \
                 return ISQL_COMMENT2; }
// fix BUG-19750
#define PLN()  { if ( g_isSIGINT == ID_TRUE )                                \
                 {                                                           \
                     g_isSIGINT = ID_FALSE;                                  \
                     BEGIN INITIAL;                                          \
                     IsPLN = ID_TRUE;                                        \
                     return ISQL_EMPTY;                                      \
                 }                                                           \
                 else                                                        \
                 {                                                           \
                     if ( IsPLN != ID_TRUE )                                 \
                     {                                                       \
                         if ( idlOS::strlen(gBufMgr->GetBuf()) == 0 )        \
                         {                                                   \
                             IsPLN = ID_TRUE;                                \
                             return ISQL_EMPTY;                              \
                         }                                                   \
                         else                                                \
                         {                                                   \
                             PLNCnt++;                                       \
                             if ( PLNCnt > 1 &&                              \
                                  gSQLCompiler->IsFileRead() == ID_FALSE &&  \
                                  g_IsProc == ID_FALSE)                      \
                             {                                               \
                                 BEGIN INITIAL;                              \
                                 IsPLN = ID_TRUE;                            \
                                 return ISQL_EMPTY;                          \
                             }                                               \
                             else                                            \
                             {                                               \
                                 gBufMgr->Append(yytext);                    \
                                 gSQLCompiler->PrintLineNum();               \
                                 IsPLN = ID_TRUE;                            \
                             }                                               \
                         }                                                   \
                     }                                                       \
                     else                                                    \
                     {                                                       \
                         IsPLN = ID_FALSE;                                   \
                     }                                                       \
                 }                                                           \
               }
#define PLN2() { if ( g_isSIGINT == ID_TRUE )                            \
                 {                                                       \
                     g_isSIGINT = ID_FALSE;                              \
                     BEGIN INITIAL;                                      \
                     IsPLN = ID_TRUE;                                    \
                     return ISQL_EMPTY;                                  \
                 }                                                       \
                 else                                                    \
                 {                                                       \
                     if ( IsPLN != ID_TRUE )                             \
                     {                                                   \
                         if ( idlOS::strlen(gBufMgr->GetBuf()) == 0 )    \
                         {                                               \
                             IsPLN = ID_TRUE;                            \
                             return ISQL_EMPTY;                          \
                         }                                               \
                         else                                            \
                         {                                               \
                             gBufMgr->Append(yytext);                    \
                             gSQLCompiler->PrintLineNum();               \
                             IsPLN = ID_TRUE;                            \
                         }                                               \
                     }                                                   \
                     else                                                \
                     {                                                   \
                         IsPLN = ID_FALSE;                               \
                     }                                                   \
                 }                                                       \
               }
#define ICP()  { if ( g_isSIGINT == ID_TRUE )                            \
                 {                                                       \
                     g_isSIGINT = ID_FALSE;                              \
                     BEGIN INITIAL;                                      \
                     IsPLN = ID_TRUE;                                    \
                     return ISQL_EMPTY;                                  \
                 }                                                       \
                 else                                                    \
                 {                                                       \
                     if ( IsPLN != ID_TRUE )                             \
                     {                                                   \
                         if ( idlOS::strlen(gBufMgr->GetBuf()) == 0 )    \
                         {                                               \
                             IsPLN = ID_TRUE;                            \
                             return ISQL_EMPTY;                          \
                         }                                               \
                         else                                            \
                         {                                               \
                             gBufMgr->Append(yytext);                    \
                             IsPLN = ID_TRUE;                            \
                             return IDE_FAILURE;                         \
                         }                                               \
                     }                                                   \
                     IsPLN = ID_FALSE;                                   \
                 }                                                       \
               }


/*
 * BUGBUG
 * for some reason, ECHO was defined badly as 0000010
 * the suspects as the followings..
 * /usr/include/asm-i386/termbits.h:#define ECHO   0000010
 * /usr/include/asm-x86_64/termbits.h:#define ECHO 0000010
 * /usr/include/bits/termios.h:#define ECHO        0000010
 * So, I remove define of ECHO as the following for removing compiler warnings..
 * Then, flex can define ECHO correctly..
 */
#if defined(ECHO)
# undef ECHO
#endif

%}

%x xc
%x xc2
%x xc3
%x xc4
%x xc5
%x xc6
%x xc7
%x xc8
%x xc9
%x xc10
%x xstr
%x xstr2
%x xstr3
%x xstr4
%x xQuoStr
%x xslash
%x xslash2
%x xslash3
%x xslash4
%x xhyphen
%x xhyphen2
%x xhyphen3
%x xhyphen4
%x xhyphen5
%x xhyphen6
%x xhyphen7
%x xhyphen8
%x xsql
%x xset
%x xproc
%x xcreate

sqlCom      ("ALTER"|"AUDIT"|"NOAUDIT"|"DELAUDIT"|"CHECK"|"COMMENT"|"COMMIT"|"DELETE"|"DROP"|"EXEC"|"EXECUTE"|"GRANT"|"INSERT"|"LOCK"|"MOVE"|"MERGE"|"RENAME"|"REVOKE"|"ROLLBACK"|"SAVEPOINT"|"SELECT"|"TRUNCATE"|"UPDATE"|"PREPARE"|"ENQUEUE"|"DEQUEUE"|"WITH"|"PURGE"|"FLASHBACK"|"DISJOIN"|"CONJOIN"|"SHARD"|"NODE"[ \t\n]*"["[^]]*"]")
isqlCom     ("@"|"!"|"AUTOCOMMIT"|"CLEAR"|"CONNECT"|"COL"|"COLUMN"|"DESC"|"DISCONNECT"|"ED"|"EDIT"|"EXIT"|"H"|"HELP"|"HISTORY"|"LOAD"|"PRINT"|"QUIT"|"SAVE"|"SHOW"|"START"|"SPOOL"|"VAR")
create      ("CREATE"[ ]*)
createProc  ("FUNCTION"|"PROCEDURE"|"TRIGGER"|"PACKAGE"|"OR"[ \t\n]+"REPLACE"[ \t\n]+"FUNCTION"|"OR"[ \t\n]+"REPLACE"[ \t\n]+"PROCEDURE"|"OR"[ \t\n]+"REPLACE"[ \t\n]+"TRIGGER"|"TYPESET"|"OR"[ \t\n]+"REPLACE"[ \t\n]+"TYPESET"|"OR"[ \t\n]+"REPLACE"[ \t\n]+"PACKAGE")
createObj   ("AGENTEVENT"|"INDEX"|"QUEUE"|"REPLICATION"|"SEQUENCE"|"TABLE"|"UNIQUE"|"USER"|"VIEW"|"OR REPLACE NO"|"OR REPLACE FORCE""OR REPLACE VIEW")
set         ("SET")
isqlOption  ("HEADING"|"COLSIZE"|"LINESIZE"|"NUM"|"NUMWIDTH"|"PAGESIZE"|"TERM"|"TIMESCALE"|"TIMING"|"VERBOSE"|"FEEDBACK"|"FOREIGNKEYS"|"CHECKCONSTRAINTS"|"FULLNAME"|"LOBOFFSET"|"LOBSIZE"|"VERTICAL"|"PLANCOMMIT"|"QUERYLOGGING"|"ECHO"|"SQLP"|"SQLPROMPT"|"DEFINE"|"PARTITIONS"|"VERIFY"|"PREFETCHROWS"|"ASYNCPREFETCH")
adminCom    ("STARTUP"|"SHUTDOWN")

%%

"/*"                                    { // multi line comment
                                          BEGIN xc;        APD();  }
<xc>[*]+                                { BEGIN xc2;       APD();  }
<xc>[^*\n]*                             {                  APD();  }
<xc>\r?\n                               {                  PLN2(); }
<xc2>"/"                                { BEGIN INITIAL;   RTC2(); }
<xc2>"/"[ \t]*\r?\n                     { BEGIN INITIAL;   RTC();  }
<xc2>[^/\n]                             { BEGIN xc;        APD();  }
<xc2>\r?\n                              { BEGIN xc;        PLN2(); }

"--"[^\n]*\r?\n                         { // single line comment
                                                           RTC();  }
"//"[^\n]*\r?\n                         { // single line comment
                                                           RTC();  }

{adminCom}[^\n]*\r?\n                   { // admin command
                                                           RTN();  }
{isqlCom}[^\n]*\r?\n                    { // isql command
                                                           RTN();  }
[0-9]*[ \t]*"/"[ \t]*;?\r?\n            { // hisrun command
                                                           RTN();  }
[0-9]+[ \t]*("ED"|"EDIT")[ \t]*;?\r?\n  { // hisedit command
                                                           RTN();  }

{sqlCom}                                { // sql command
                                          BEGIN xsql;      APD();  }

<xsql>'                                 { // string in query
                                          BEGIN xstr;      APD();  }

<xstr>("''"|[^'\n])*'                   { BEGIN xsql;      APD();  }
<xstr>("''"|[^'\n])*\r?\n               {                  PLN2(); }

<xsql>\"                                { // Quoted String
                                          // To Fix BUG-17430
                                          // Quoted String�� �ؼ��� �� �־�� ��.
                                          // ���� ������ �ּ����� ó���� �� ����
                                          // Ex) "NAME--NAME";
                                          BEGIN xQuoStr;   APD();  }
<xQuoStr>[^\"\n]*\"                     { /*BUG-25932*/ BEGIN xsql;      APD();  }
<xQuoStr>[^\"\n]*\r?\n                  { /*BUG-25932*/                 PLN2(); }

<xsql>"/"                               { BEGIN xslash;    APD();  }
<xslash>[*]                             { // multi line comment in query
                                          BEGIN xc3;       APD();  }
<xslash>'                               { BEGIN xstr;      APD();  }
<xslash>"/"[^\n]*\r?\n                  { // single line comment in query
                                          BEGIN xsql;      PLN();  }
<xslash>[^'/*\n]                        { BEGIN xsql;      APD();  }
<xslash>\r?\n                           { BEGIN xsql;      PLN();  }
<xc3>[*]+                               { BEGIN xc4;       APD();  }
<xc3>[^*\n]*                            {                  APD();  }
<xc3>\r?\n                              {                  PLN();  }
<xc4>"/"                                { // end of comment
                                          BEGIN xsql;      APD();  }
<xc4>[^/\n]                             { BEGIN xc3;       APD();  }
<xc4>\r?\n                              { BEGIN xc3;       PLN();  }

<xsql>"-"                               { BEGIN xhyphen;   APD();  }
<xhyphen>"-"                            { // single line comment in query
                                          BEGIN xhyphen2;  APD();  }
<xhyphen>"'"                            { BEGIN xstr;      APD();  }
<xhyphen>"/"                            { BEGIN xslash;    APD();  }
<xhyphen>[^'/\n]                        { BEGIN xsql;      APD();  }
<xhyphen2>[^\n]*\r?\n                   { BEGIN xsql;      PLN();  }
<xhyphen>\r?\n                          { BEGIN xsql;      PLN();  }

<xsql>[^-'"/;\n]*                       { // query
                                          // BUG-17430 Quoted String �� �и�
                                                           APD();  }
<xsql>;[ \t]*(("--"|"//")[^\n]*)?\r?\n  { // end of query
                                          BEGIN INITIAL;   RTN();  }
<xsql>;                                 { // semicolon in query
                                                           APD();  }
<xsql>\r?\n                             { // newline in query
                                                           PLN();  }

{create}                                { BEGIN xcreate;   APD();  }

<xcreate>'                              { // string after 'create'
                                          BEGIN xstr2;     APD();  }
<xstr2>("''"|[^'\n])*'                  { BEGIN xcreate;   APD();  }
<xstr2>("''"|[^'\n])*\r?\n              {                  PLN2(); }

<xcreate>"/"                            { BEGIN xslash2;   APD();  }
<xslash2>[*]                            { // multi line comment after 'create'
                                          BEGIN xc5;       APD();  }
<xslash2>'                              { BEGIN xstr2;     APD();  }
<xslash2>"/"[^\n]*\r?\n                 { // single line comment after 'create'
                                          BEGIN xcreate;   PLN();  }
<xslash2>[^'/*\n]                       { BEGIN xcreate;   APD();  }
<xslash2>\r?\n                          { BEGIN xcreate;   PLN();  }
<xc5>[*]+                               { BEGIN xc6;       APD();  }
<xc5>[^*\n]*                            {                  APD();  }
<xc5>\r?\n                              {                  PLN();  }
<xc6>"/"                                { // end of comment
                                          BEGIN xcreate;   APD();  }
<xc6>[^/\n]                             { BEGIN xc5;       APD();  }
<xc6>\r?\n                              { BEGIN xc5;       PLN();  }

<xcreate>"-"                            { BEGIN xhyphen3;  APD();  }
<xhyphen3>"-"                           { // single line comment after 'create'
                                          BEGIN xhyphen4;  APD();  }
<xhyphen3>"'"                           { BEGIN xstr2;     APD();  }
<xhyphen3>"/"                           { BEGIN xslash2;   APD();  }
<xhyphen3>[^'/\n]                       { BEGIN xcreate;   APD();  }
<xhyphen4>[^\n]*\r?\n                   { BEGIN xcreate;   PLN();  }
<xhyphen3>\r?\n                         { BEGIN xcreate;   PLN();  }

<xcreate>[ \t]*{createProc}             { // create procedure command
                                          BEGIN xproc;     g_IsProc = ID_TRUE;
                                                           APD();  }
<xcreate>[ \t]*[^- '/;\t\n]*            { // create object command
                                          BEGIN xsql;      APD();  }
<xcreate>[^-'/\n]                       { /* space or tab or invalid command
                                           * after 'create' */
                                                           APD();  }
<xcreate>\r?\n                          { // newline after 'create'
                                                           PLN();  }

<xproc>'                                { // string in create procedure stmt
                                          BEGIN xstr3;     APD();  }
<xstr3>("''"|[^'\n])*'                  { BEGIN xproc;     APD();  }
<xstr3>("''"|[^'\n])*\r?\n              {                  PLN();  }

<xproc>"/"                              { BEGIN xslash3;   APD();  }
<xslash3>[*]                            { /* multi line comment in
                                           * create procedure stmt */
                                          BEGIN xc7;       APD();  }
<xslash3>'                              { BEGIN xstr3;     APD();  }
<xslash3>"/"[^\n]*\r?\n                 { /* single line comment in
                                           * create procedure stmt */
                                          BEGIN xproc;     PLN();  }
<xslash3>[^'/*\n]                       { BEGIN xproc;     APD();  }
<xslash3>\r?\n                          { BEGIN xproc;     PLN();  }
<xc7>[*]+                               { BEGIN xc8;       APD();  }
<xc7>[^*\n]*                            {                  APD();  }
<xc7>\r?\n                              {                  PLN();  }
<xc8>"/"                                { // end of comment
                                          BEGIN xproc;     APD();  }
<xc8>[^/\n]                             { BEGIN xc7;       APD();  }
<xc8>\r?\n                              { BEGIN xc7;       PLN();  }

<xproc>"-"                              { BEGIN xhyphen5;   APD();  }
<xhyphen5>"-"                           { /* single line comment in
                                           * create procedure stmt */
                                          BEGIN xhyphen6;  APD();  }
<xhyphen5>"'"                           { BEGIN xstr3;     APD();  }
<xhyphen5>"/"                           { BEGIN xslash3;   APD();  }
<xhyphen5>[^'/\n]                       { BEGIN xproc;     APD();  }
<xhyphen6>[^\n]*\r?\n                   { BEGIN xproc;     PLN();  }
<xhyphen5>\r?\n                         { BEGIN xproc;     PLN();  }

<xproc>[^-'/\n]*                        { // create procedure stmt
                                                           APD();  }
<xproc>\r?\n[ \t]*"/"[ \t]*\r?\n        { // end of create procedure stmt
                                          BEGIN INITIAL;   g_IsProc = ID_FALSE;
                                                           RTN2();  }
<xproc>\r?\n                            { // newline in create procedure stmt
                                                           PLN();  }

{set}                                   { BEGIN xset;      APD();  }

<xset>'                                 { // string after 'set'
                                          BEGIN xstr4;     APD();  }
<xstr4>("''"|[^'\n])*'                  { BEGIN xset;      APD();  }
<xstr4>("''"|[^'\n])*\r?\n              {                  PLN2(); }

<xset>"/"                               { BEGIN xslash4;   APD();  }
<xslash4>[*]                            { // multi line comment after 'set'
                                          BEGIN xc9;       APD();  }
<xslash4>'                              { BEGIN xstr4;     APD();  }
<xslash4>"/"[^\n]*\r?\n                 { // single line comment after 'set'
                                          BEGIN xset;      PLN();  }
<xslash4>[^'/*\n]                       { BEGIN xset;      APD();  }
<xslash4>\r?\n                          { BEGIN xset;      PLN();  }
<xc9>[*]+                               { BEGIN xc10;      APD();  }
<xc9>[^*\n]*                            {                  APD();  }
<xc9>\r?\n                              {                  PLN();  }
<xc10>"/"                               { // end of comment
                                          BEGIN xset;      APD();  }
<xc10>[^/\n]                            { BEGIN xc9;       APD();  }
<xc10>\r?\n                             { BEGIN xc9;       PLN();  }

<xset>"-"                               { BEGIN xhyphen7;   APD();  }
<xhyphen7>"-"                           { // single line comment after 'set'
                                          BEGIN xhyphen8;  APD();  }
<xhyphen7>"'"                           { BEGIN xstr4;     APD();  }
<xhyphen7>"/"                           { BEGIN xslash4;   APD();  }
<xhyphen7>[^'/\n]                       { BEGIN xset;      APD();  }
<xhyphen8>[^\n]*\r?\n                   { BEGIN xset;      PLN();  }
<xhyphen7>\r?\n                         { BEGIN xset;      PLN();  }

<xset>[ \t]*{isqlOption}[^\n]*\r?\n     { // set option command
                                          BEGIN INITIAL;   RTN();  }
<xset>[ \t]*[^- '/;\t\n]*               { // set command
                                          BEGIN xsql;      APD();  }
<xset>\r?\n                             { // newline after 'set'
                                          BEGIN INITIAL;   RTN();  }

.                                       { // invalid command
                                                           APD();  }
\r?\n                                   {                  ICP();  }

%%

#ifdef USE_READLINE
SInt iSQLReadLine( SChar * buf )
{
    SInt          ch_cnt = 0;
    const char   *in_ch;

    in_ch = el_gets(gEdo, &ch_cnt);

    if ((in_ch == NULL) || (ch_cnt == 0))
    {
        /* bug 18731 */
        Exit(gLastReturnValue);
    }

    if (*in_ch != '\n')
    {
        history(gHist, &gEvent, H_ENTER, in_ch);
    }

    idlOS::strncpy(buf, in_ch, ch_cnt);
    buf[ch_cnt++] = '\n';
    buf[ch_cnt] = '\0';

    return ch_cnt;
}
#endif

SInt
iSQLPreLexer_input( SChar * buf,
                    SInt    /* max_size */ )
{
    SInt    i;
    SChar   ch;
    FILE  * fp = NULL;

    i = 0;

AGAIN:
    /* ============================================
     * end-of-file�̸� ������ ó���ϴ� ������ m_flist�� �����Ѵ�.
     * ���� ������ ó���ϴ� ������ ���ٸ� isql�� �����Ѵ�.
     * ============================================ */
    if ( IsEOF == ID_TRUE )
    {
        if ( idlOS::strlen(gBufMgr->GetBuf()) != 0 )
        {
            gSQLCompiler->PrintCommand();
            gBufMgr->Reset();
            idlOS::sprintf(gSpool->m_Buf,
                    "[Error] Unterminated command.\n");
            gSpool->Print();
            if (g_inLoad == ID_TRUE || g_inEdit == ID_TRUE)
            {
                BEGIN INITIAL;
                IsEOF = ID_FALSE;
                buf[i] = '\0';
                g_inLoad = ID_FALSE;
                g_inEdit = ID_FALSE;
                gSQLCompiler->ResetInput();
                return i;
            }
        }

        if ( gSQLCompiler->ResetInput() != IDE_SUCCESS )
        {
            /* bug 18731 */
            Exit(gLastReturnValue);
        }
        BEGIN INITIAL;
        IsEOF = ID_FALSE;

        /* ============================================
         * FileRead�� �ƴϸ� stdin�̹Ƿ� ������Ʈ�� ����Ѵ�.
         * FileRead ���δ� PrintPrompt()���� üũ�Ѵ�.
         * ============================================ */
         gSQLCompiler->PrintPrompt();
    }

    fp = gSQLCompiler->m_flist->fp;
#ifdef USE_READLINE
    if ( (fp == stdin) && (gProgOption.UseLineEditing() == ID_TRUE) )
    {
        return iSQLReadLine(buf);
    }
    else
    {
#endif
    /* ============================================
     * fp�κ��� �ѹ��ھ� �о� buf�� �����Ѵ�.
     * newline�� ������ newline�� �ϳ� �� �ٿ��� �����Ѵ�.
     * newline�� �Ѱ��� ��� ���ϸ�ġ���� ����. (maybe flex bug)
     * ============================================ */
    while ( ID_TRUE )
    {
        // NULL-term���� �ִ� 3���ڸ� ���ۿ� �� �� �����Ƿ� ������ �ִ��� Ȯ��
        if ((i+3) > YY_BUF_SIZE)
        {
            gBufMgr->Reset();
	    // BUG-32613 The English grammar of the iSQL message "Have no saved command." needs to be corrected.
            idlOS::sprintf(gSpool->m_Buf, "[Error] SQL statement too long.\n");
            gSpool->Print();
            yy_flush_buffer(YY_CURRENT_BUFFER);
            buf[0] = '\0';
            ICP();
            break;
        }

        ch = getc(fp);
        if ( feof(fp) != 0 )
        {
            IsEOF = ID_TRUE;
            if ( g_glogin == ID_TRUE )
            {
                g_glogin = ID_FALSE;
            }
            else if ( g_login == ID_TRUE )
            {
                g_login = ID_FALSE;
            }
            if ( i == 0 && idlOS::strlen(gBufMgr->GetBuf()) == 0 )
            {
                goto AGAIN;
            }
            else
            {
                /* ============================================
                 * ���ɾ ����ǰ� �������� EOF�� ���� ���.
                 * ============================================ */
                buf[i++] = '\n';
                buf[i++] = '\n';
                break;
            }
        }
        else if ( ch == '\n' )
        {
            buf[i++] = ch;
            buf[i++] = ch;
            break;
        }
        else
        {
            buf[i++] = ch;
        }
    }

    buf[i] = '\0';

    return i;
#ifdef USE_READLINE
    }
#endif
}