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
 * $Id: qsvProcStmts.cpp 82075 2018-01-17 06:39:52Z jina.kim $
 **********************************************************************/

#include <idl.h>
#include <ide.h>
#include <qcmCache.h>
#include <qcmUser.h>
#include <qcmTableInfo.h>
#include <qcuSqlSourceInfo.h>
#include <qsvPkgStmts.h>
#include <qsvProcStmts.h>
#include <qsvProcVar.h>
#include <qsvProcType.h>
#include <qsvCursor.h>
#include <qsvRefCursor.h>
#include <qsvEnv.h>
#include <qsxExecutor.h>
#include <qsxUtil.h>
#include <qsv.h>
#include <qso.h>
#include <qmv.h>
#include <qcmSynonym.h>
#include <qcpManager.h>
#include <qcd.h>
#include <qmvWith.h>
#include <qdpPrivilege.h>
#include <qdpRole.h>

extern mtdModule    mtdBoolean;
extern mtdModule    mtdVarchar;
extern mtdModule    mtdChar;
extern mtdModule    mtdInteger;

IDE_RC qsvProcStmts::parse(
    qcStatement * /*aStatement*/,
    qsProcStmts * /*aProcStmts*/)
{
    /* Nothing to do. */

    return IDE_SUCCESS;
}

IDE_RC qsvProcStmts::parseBlock(
    qcStatement * /* aStatement */,
    qsProcStmts * /* aProcStmts */ )
{
    /* Nothing to do. */

    return IDE_SUCCESS;
}

IDE_RC qsvProcStmts::validateBlock( qcStatement * aStatement,
                                    qsProcStmts * aProcStmts,
                                    qsProcStmts * aParentStmt,
                                    qsValidatePurpose aPurpose )
{
    qsProcParseTree     * sParseTree;
    qsProcStmtBlock     * sBLOCK = (qsProcStmtBlock *)aProcStmts;
    qsProcStmts         * sProcStmt;
    qsVariableItems     * sCurrItem;
    qsVariableItems     * sNextItem;
    qsVariableItems     * sParaDef;
    qsCursors           * sCursor;
    qsAllVariables      * sOldAllVariables;
    qcuSqlSourceInfo      sqlInfo;
    // fix BUG-32837
    qsVariableItems     * sOldAllParaDecls;

    QS_SET_PARENT_STMT( aProcStmts, aParentStmt );

    aStatement->spvEnv->currStmt = aProcStmts;

    sParseTree = aStatement->spvEnv->createProc;

    // BUG-37364
    // package�� initialize section���� declare block ��� ��
    // qsProcParseTree�� NULL�̴�.
    if( sParseTree != NULL )
    {
        // fix BUG-32837
        sOldAllParaDecls = aStatement->spvEnv->allParaDecls;
        aStatement->spvEnv->allParaDecls = sParseTree->paraDecls;
    }
    else
    {
        // Nothing to do.
        // package�� initialize section�� parameter�� �������� �ʴ´�.
    }

    IDE_TEST(connectAllVariables( aStatement,
                                  sBLOCK->common.parentLabels,
                                  sBLOCK->variableItems,
                                  ID_FALSE,
                                  &sOldAllVariables)
             != IDE_SUCCESS);

    // exception
    for (sCurrItem = sBLOCK->variableItems;
         sCurrItem != NULL;
         sCurrItem = sCurrItem->next)
    {
        if (sCurrItem->itemType == QS_EXCEPTION)
        {
            // BUG-27442
            // Validate length of Label name
            if ( sCurrItem->name.size > QC_MAX_OBJECT_NAME_LEN )
            {
                sqlInfo.setSourceInfo( aStatement,
                                       & sCurrItem->name );
                IDE_RAISE( ERR_MAX_NAME_LEN_OVERFLOW );
            }
            else
            {
                // Nothing to do.
            }

            // check duplicate exception name
            for (sNextItem = sCurrItem->next;
                 sNextItem != NULL;
                 sNextItem = sNextItem->next)
            {
                if (sNextItem->itemType == QS_EXCEPTION)
                {
                    if (idlOS::strMatch(
                            sNextItem->name.stmtText + sNextItem->name.offset,
                            sNextItem->name.size,
                            sCurrItem->name.stmtText + sCurrItem->name.offset,
                            sCurrItem->name.size) == 0)
                    {
                        sqlInfo.setSourceInfo( aStatement,
                                               & sCurrItem->name );
                        IDE_RAISE(ERR_DUP_VARIABLE_ITEM);
                    }
                }
            }

            QS_INIT_EXCEPTION_VAR( aStatement, (qsExceptionDefs *)sCurrItem );
        }
    }

    // local variables, cursor
    // PROJ-1075 type �̸��� �ߺ��� �� ����.(������ ȥ��Ұ�)
    for (sCurrItem = sBLOCK->variableItems;
         sCurrItem != NULL;
         sCurrItem = sCurrItem->next)
    {
        // To fix BUG-14129
        // ���� validate���� declare item�� ��ũ.
        aStatement->spvEnv->currDeclItem = sCurrItem;

        if ( ( sCurrItem->itemType == QS_VARIABLE ) ||
             ( sCurrItem->itemType == QS_TRIGGER_VARIABLE ) ||
             ( sCurrItem->itemType == QS_CURSOR ) ||
             ( sCurrItem->itemType == QS_TYPE ) )
        {
            // check duplicate other item name
            for (sNextItem = sCurrItem->next;
                 sNextItem != NULL;
                 sNextItem = sNextItem->next)
            {
                if ( ( sNextItem->itemType == QS_VARIABLE ) ||
                     ( sNextItem->itemType == QS_TRIGGER_VARIABLE ) ||
                     ( sNextItem->itemType == QS_CURSOR ) ||
                     ( sNextItem->itemType == QS_TYPE ) )
                {
                    if ( QC_IS_NAME_MATCHED( sNextItem->name, sCurrItem->name ) )
                    {
                        sqlInfo.setSourceInfo( aStatement,
                                               & sCurrItem->name );
                        IDE_RAISE(ERR_DUP_VARIABLE_ITEM);
                    }
                }
            }

            if( sParseTree != NULL )  // BUG-37364
            {
                if (sParseTree->block == sBLOCK)    // only ROOT block
                {
                    // check duplicate parameter name
                    for (sParaDef = sParseTree->paraDecls;
                         sParaDef != NULL;
                         sParaDef = sParaDef->next)
                    {
                        if ( QC_IS_NAME_MATCHED( sParaDef->name, sCurrItem->name ) )
                        {
                            sqlInfo.setSourceInfo( aStatement,
                                                   & sCurrItem->name );
                            IDE_RAISE(ERR_DUP_VARIABLE_ITEM);
                        }
                    }
                }
            }

            if ( (sCurrItem->itemType == QS_VARIABLE) ||
                 (sCurrItem->itemType == QS_TRIGGER_VARIABLE) )
            {
                IDE_TEST(qsvProcVar::validateLocalVariable(
                             aStatement,
                             (qsVariables *)sCurrItem)
                         != IDE_SUCCESS);
            }
            else if (sCurrItem->itemType == QS_CURSOR)
            {
                sCursor = (qsCursors *)sCurrItem;
                IDE_TEST( parseCursor( aStatement,
                                       sCursor ) != IDE_SUCCESS );

                IDE_TEST(qsvCursor::validateCursorDeclare(
                             aStatement,
                             (qsCursors *)sCurrItem)
                         != IDE_SUCCESS);
            }
            else if (sCurrItem->itemType == QS_TYPE)
            {
                IDE_TEST( qsvProcType::validateTypeDeclare(
                              aStatement,
                              (qsTypes*)sCurrItem,
                              & sCurrItem->name,
                              ID_FALSE )
                          != IDE_SUCCESS );
            }
        }
        else
        {
            switch ( sCurrItem->itemType )
            {
                /* BUG-43112
                   __autonomous_transaction_pragma_disable property�� 
                   0(default)�̸� AT��� ����, 1�̸� ����� �������� �ʴ´�. */
                case QS_PRAGMA_AUTONOMOUS_TRANS:
                    if ( QCU_AUTONOMOUS_TRANSACTION_PRAGMA_DISABLE == 0 )
                    {
                        // qsProcStmtsBlock�� �ֻ������� �Ѵ�. 
                        if ( aParentStmt == NULL )
                        {
                            for ( sNextItem = sCurrItem->next;
                                  sNextItem != NULL;
                                  sNextItem = sNextItem->next )
                            {
                                if ( sNextItem->itemType == QS_PRAGMA_AUTONOMOUS_TRANS )
                                {
                                    sqlInfo.setSourceInfo( aStatement,
                                                           & sNextItem->name );
                                    IDE_RAISE( ERR_CANNOT_DUP_PRAGMA_AUTONOMOUS_TRANS );
                                }
                                else
                                {
                                    // Nothing to do.
                                }
                            }

                            sBLOCK->isAutonomousTransBlock = ID_TRUE;
                        }
                        else
                        {
                            sqlInfo.setSourceInfo( aStatement,
                                                   & sCurrItem->name );
                            IDE_RAISE( ERR_CANNOT_SPECIFIED_PRAGMA_AUTONOMOUS_TRANS );
                        }
                    }
                    else
                    {
                        // Nothing to do.
                    }
                    break;
                /* BUG-41240 EXCEPTION_INIT Pragma */
                case QS_PRAGMA_EXCEPTION_INIT:
                    IDE_TEST( validatePragmaExcepInit( aStatement,
                                                       sBLOCK->variableItems,
                                                       (qsPragmaExceptionInit*)sCurrItem )
                              !=IDE_SUCCESS ); 
                    break;
                default:
                    break;
            }
        }
    }

    /* BUG-34112
       �׽�Ʈ�� ���� ���������� AT �����մϴ�. ���������� �Ʒ��� �����ϴ�.
           1. �ֻ��� psm block �� ��
           2. autonomous_transaction_pragam disable�� 0(default)�� ��
           3. __force_autonomous_transaction_pragma�� 1 �� ��
           4. ������ AT�� ID_FALSE �� �� => �̹� ������ ���¿��� �缳�� ���� �ʴ´�. */
    if ( (aParentStmt == NULL) &&
         (QCU_AUTONOMOUS_TRANSACTION_PRAGMA_DISABLE == 0) &&
         (QCU_FORCE_AUTONOMOUS_TRANSACTION_PRAGMA == 1) &&
         (sBLOCK->isAutonomousTransBlock == ID_FALSE) )
    {
        sBLOCK->isAutonomousTransBlock = ID_TRUE;
    }
    else
    {
        // Nothing to do.
    }

    // To fix BUG-14129
    // declare item�� validation �Ϸ�.
    // ��ũ�� ����.
    aStatement->spvEnv->currDeclItem = NULL;

    // parse & validate body
    for (sProcStmt = sBLOCK->bodyStmts;
         sProcStmt != NULL;
         sProcStmt = sProcStmt->next)
    {
        IDE_TEST(sProcStmt->parse(aStatement, sProcStmt) != IDE_SUCCESS);

        IDE_TEST(sProcStmt->validate(aStatement, sProcStmt, aProcStmts, aPurpose )
                 != IDE_SUCCESS);
    }

    if( sBLOCK->exception != NULL )
    {
        // exception handler
        // PROJ-1335, To fix BUG-12475
        // exception handler �ȿ����� �ڽ��� block���� goto �� �� ����
        // �ڽ��� ���� block���� goto�� �� �ֱ� ������
        // parent statement�� ���ڷ� �ڽ��� statement�� �ִ� ���� �ƴ϶�
        // �ڽ��� parent statement�� �ѱ��.
        IDE_TEST(validateExceptionHandler( aStatement, sBLOCK, NULL, aParentStmt, aPurpose )
                 != IDE_SUCCESS);
    }

    disconnectAllVariables( aStatement, sOldAllVariables );

    // BUG-37364
    // package�� initialize section���� declare block ��� ��
    // qsProcParseTree�� NULL�̴�.
    if( sParseTree != NULL )
    {
        // fix BUG-32837
        aStatement->spvEnv->allParaDecls = sOldAllParaDecls;
    }
    else
    {
        // Nothing to do.
        // package�� initialize section�� parameter�� �������� �ʴ´�.
    }

    return IDE_SUCCESS;

   
    IDE_EXCEPTION(ERR_DUP_VARIABLE_ITEM);
    {
        (void)sqlInfo.init(aStatement->qmeMem);
        IDE_SET(
            ideSetErrorCode(qpERR_ABORT_QSV_DUPLICATE_VARIABLE_SQLTEXT,
                            sqlInfo.getErrMessage() ));
        (void)sqlInfo.fini();
    }
    IDE_EXCEPTION(ERR_MAX_NAME_LEN_OVERFLOW)
    {
        (void)sqlInfo.init(aStatement->qmeMem);
        IDE_SET(
            ideSetErrorCode(qpERR_ABORT_QCP_MAX_NAME_LENGTH_OVERFLOW,
                            sqlInfo.getErrMessage() ));
        (void)sqlInfo.fini();
    }
    IDE_EXCEPTION(ERR_CANNOT_DUP_PRAGMA_AUTONOMOUS_TRANS)
    {
        (void)sqlInfo.init(aStatement->qmeMem);
        IDE_SET(
            ideSetErrorCode(qpERR_ABORT_QSV_CANNOT_DECLARE_TWICE_PRAGMA_AUTONOMOUS_TRANSACTION,
                            sqlInfo.getErrMessage() ));
        (void)sqlInfo.fini();
    }
    IDE_EXCEPTION(ERR_CANNOT_SPECIFIED_PRAGMA_AUTONOMOUS_TRANS)
    {
        (void)sqlInfo.init(aStatement->qmeMem);
        IDE_SET(
            ideSetErrorCode(qpERR_ABORT_QSV_CANNOT_SPECIFIED_PRAGMA_AUTONOMOUS_TRANSACTION,
                            sqlInfo.getErrMessage() ));
        (void)sqlInfo.fini();
    }
    IDE_EXCEPTION_END;

    return IDE_FAILURE;
}

IDE_RC qsvProcStmts::parseSql( qcStatement * aStatement,
                               qsProcStmts * aProcStmts )
{
    qsProcStmtSql   * sSQL;
    qcStatement     * sStatement;
    qcuSqlSourceInfo  sqlInfo;

    QSV_ENV_SET_SQL( aStatement, aProcStmts );

    // BUG-37878 with���� ������ parse �ܰ迡���� validate�� �����ϰ�,
    // sysdate�� ����ϸ� setUseDate �Լ����� currStmt�� ����ϹǷ� �����Ѵ�.
    aStatement->spvEnv->currStmt = aProcStmts;

    // qcStatement �ٽ� ����
    IDE_TEST( initSqlStmtForParse( aStatement,
                                   aProcStmts ) != IDE_SUCCESS );

    sSQL = (qsProcStmtSql *)aProcStmts;

    sStatement = sSQL->statement;

    sStatement->mStatistics = aStatement->mStatistics;

    IDE_TEST( sStatement->myPlan->parseTree->parse( sStatement ) != IDE_SUCCESS );

    if( qsvProcStmts::isSQLType( sSQL->common.stmtType ) == ID_TRUE )
    {
        IDE_TEST( qcg::fixAfterParsing( sStatement ) != IDE_SUCCESS );
    }

    QSV_ENV_INIT_SQL( aStatement );
    
    return IDE_SUCCESS;

  
    IDE_EXCEPTION_END;

    // set original error code.
    qsxEnv::setErrorCode( QC_QSX_ENV(aStatement) );

    // BUG-43998
    // PSM ���� ���� �߻��� ���� �߻� ��ġ�� �� ���� ����ϵ��� �մϴ�.
    if ( ideHasErrorPosition() == ID_FALSE )
    {
        sqlInfo.setSourceInfo( aStatement,
                               &aProcStmts->pos );

        (void)sqlInfo.initWithBeforeMessage(
            aStatement->qmeMem );

        IDE_SET(
            ideSetErrorCode(qpERR_ABORT_QSX_SQLTEXT_WRAPPER,
                            sqlInfo.getBeforeErrMessage(),
                            sqlInfo.getErrMessage()));
        (void)sqlInfo.fini();
    }

    // set sophisticated error message.
    qsxEnv::setErrorMessage( QC_QSX_ENV(aStatement) );

    QSV_ENV_INIT_SQL( aStatement );

    return IDE_FAILURE;
}

IDE_RC qsvProcStmts::initSqlStmtForParse( qcStatement * aStatement,
                                          qsProcStmts * aProcStmts )
{
    qsProcStmtSql   * sSQL       = (qsProcStmtSql *)aProcStmts;
    qcStatement     * sStatement = NULL;

    IDE_TEST( STRUCT_CRALLOC( QC_QME_MEM(aStatement), qcStatement, &sStatement )
              != IDE_SUCCESS );

    sStatement->mStatistics = aStatement->mStatistics;

    // myPlan�� �����Ѵ�.
    sStatement->myPlan = & sStatement->privatePlan;
    sStatement->myPlan->planEnv = NULL;

    sStatement->session         = aStatement->session;
    sStatement->stmtInfo        = aStatement->stmtInfo;
    sStatement->spvEnv          = aStatement->spvEnv;
    sStatement->mRandomPlanInfo = aStatement->mRandomPlanInfo;

    QC_QMP_MEM(sStatement) = QC_QMP_MEM(aStatement);
    QC_QME_MEM(sStatement) = QC_QME_MEM(aStatement);
    QC_QMX_MEM(sStatement) = QC_QMX_MEM(aStatement);

    // fix BUG-37537
    sStatement->myPlan->stmtListMgr = aStatement->myPlan->stmtListMgr;

    QC_SHARED_TMPLATE(sStatement) = QC_SHARED_TMPLATE(aStatement);

    sStatement->myPlan->parseTree       = sSQL->parseTree;
    sStatement->myPlan->parseTree->stmt = sStatement;

    sSQL->statement = sStatement;

    return IDE_SUCCESS;

  
    IDE_EXCEPTION_END;

    return IDE_FAILURE;
}

IDE_RC qsvProcStmts::parseCursor( qcStatement * aStatement,
                                  qsCursors   * aCursor )
{
    qcuSqlSourceInfo  sqlInfo;

    qsProcParseTree * sParseTree;
    qsVariableItems * sProcPara = NULL;
    qsVariableItems * sLastCursorPara = NULL;
    UInt              sStage = 0;
    
    QSV_ENV_SET_SQL( aStatement, aCursor );

    // BUG-44716 Initialize & finalize parameters of Cursor
    // Package spec�� cursor�� package�� OID�� �־��ش�.
    if ( (aStatement->spvEnv->createPkg != NULL) &&
         (aStatement->spvEnv->createPkg->objType == QS_PKG) )
    {
        aCursor->common.objectID = aStatement->spvEnv->createPkg->pkgOID;
        aCursor->cursorTypeNode->node.objectID = aStatement->spvEnv->createPkg->pkgOID;
    }
    else
    {
        // Nothing to do.
    }

    aStatement->spvEnv->currStmt = (qsProcStmts *)aCursor->mCursorSql;
    sParseTree = aStatement->spvEnv->createProc;

    IDE_TEST( initCursorStmtForParse( aStatement,
                                      aCursor ) != IDE_SUCCESS );

    // BUG-38629 Cursor parameter should be found at 'with clause' in PSM.
    // PSM�� cursor�� ������ �� with ���� ����ϸ�
    // qmv::parseSelect���� with���� validate�Ѵ�.
    // �̶� with���� cursor parameter�� ������ �� �����Ƿ�
    // cursor parameter�� �̸� validate�ϰ� allParaDecls�� �����Ѵ�.
    if (aCursor->paraDecls != NULL)
    {
        // validateParaDef �߿� ������ �߻��� ��쿡�� error ó����
        // Error msg�� wrapper�� ������ �ʴ´�.
        sStage = 1;
        // validate parameter
        IDE_TEST(qsvProcVar::validateParaDef(aStatement, aCursor->paraDecls)
                 != IDE_SUCCESS);
        sStage = 0;

        // connect parameter list
        sLastCursorPara = aCursor->paraDecls;
        while (sLastCursorPara->next != NULL)
        {
            sLastCursorPara = sLastCursorPara->next;
        }

        if( sParseTree != NULL )
        {
            sLastCursorPara->next = sParseTree->paraDecls;
        }

        // fix BUG-32837
        sProcPara                        = aStatement->spvEnv->allParaDecls;
        aStatement->spvEnv->allParaDecls = aCursor->paraDecls;
    }
    else
    {
        // Nothing to do.
    }

    IDE_TEST( qmv::parseSelect(aCursor->statement) != IDE_SUCCESS );

    if (aCursor->paraDecls != NULL)
    {
        // disconnect parameter list
        sLastCursorPara->next = NULL;

        // fix BUG-32837
        aStatement->spvEnv->allParaDecls = sProcPara;
    }
    else
    {
        // Nothing to do.
    }

    IDE_TEST( qcg::fixAfterParsing( aCursor->statement ) != IDE_SUCCESS );

    QSV_ENV_INIT_SQL( aStatement );

    return IDE_SUCCESS;

  
    IDE_EXCEPTION_END;

    // BUG-43998
    // PSM ���� ���� �߻��� ���� �߻� ��ġ�� �� ���� ����ϵ��� �մϴ�.
    if ( sStage == 0 )
    {
        // set original error code.
        qsxEnv::setErrorCode( QC_QSX_ENV(aStatement) );

        if ( ideHasErrorPosition() == ID_FALSE )
        {
            sqlInfo.setSourceInfo( aStatement,
                                   &aCursor->pos );

            (void)sqlInfo.initWithBeforeMessage(
                aStatement->qmeMem );

            IDE_SET(
                ideSetErrorCode(qpERR_ABORT_QSX_SQLTEXT_WRAPPER,
                                sqlInfo.getBeforeErrMessage(),
                                sqlInfo.getErrMessage()));
            (void)sqlInfo.fini();
        }

        // set sophisticated error message.
        qsxEnv::setErrorMessage( QC_QSX_ENV(aStatement) );
    }
    else
    {
        // Noting to do.
    }

    QSV_ENV_INIT_SQL( aStatement );

    return IDE_FAILURE;
}

IDE_RC qsvProcStmts::initCursorStmtForParse( qcStatement * aStatement,
                                             qsCursors   * aCursor )
{
    qcStatement * sStatement = NULL;

    IDE_TEST( STRUCT_CRALLOC( QC_QME_MEM(aStatement), qcStatement, &sStatement )
              != IDE_SUCCESS );

    sStatement->mStatistics = aStatement->mStatistics;

    // myPlan�� �����Ѵ�.
    sStatement->myPlan = & sStatement->privatePlan;
    sStatement->myPlan->planEnv = NULL;

    sStatement->session         = aStatement->session;
    sStatement->stmtInfo        = aStatement->stmtInfo;
    sStatement->spvEnv          = aStatement->spvEnv;
    sStatement->mRandomPlanInfo = aStatement->mRandomPlanInfo;

    QC_QMP_MEM(sStatement) = QC_QMP_MEM(aStatement);
    QC_QME_MEM(sStatement) = QC_QME_MEM(aStatement);
    QC_QMX_MEM(sStatement) = QC_QMX_MEM(aStatement);

    // fix BUG-37537
    sStatement->myPlan->stmtListMgr = aStatement->myPlan->stmtListMgr;

    QC_SHARED_TMPLATE(sStatement) = QC_SHARED_TMPLATE(aStatement);

    sStatement->myPlan->parseTree = aCursor->mCursorSql->parseTree;

    sStatement->myPlan->parseTree->stmt = sStatement;

    aCursor->statement = sStatement;

    return IDE_SUCCESS;

  
    IDE_EXCEPTION_END;

    return IDE_FAILURE;
}

IDE_RC qsvProcStmts::validateSql( qcStatement     * aStatement,
                                  qsProcStmts     * aProcStmts,
                                  qsProcStmts     * aParentStmt,
                                  qsValidatePurpose aPurpose )
{
    qsProcStmtSql   * sSQL = (qsProcStmtSql *)aProcStmts;
    qcStatement     * sStatement;
    qmsParseTree    * sParseTree;
    UInt              sOriSubqueryArgFlag = 0;
    UInt              sStage = 0;
    qsUsingParam    * sCurrParam;
    qtcNode         * sCurrParamNode;
    qcuSqlSourceInfo  sqlInfo;
    
    QSV_ENV_SET_SQL( aStatement, aProcStmts );

    QS_SET_PARENT_STMT( aProcStmts, aParentStmt );

    aStatement->spvEnv->currStmt = aProcStmts;

    // validate SQL statement
    sStatement                  = sSQL->statement;
    sStatement->myPlan->planEnv = NULL;
    sStatement->spvEnv          = aStatement->spvEnv;
    sStatement->mRandomPlanInfo = aStatement->mRandomPlanInfo;

    // no subquery is allowed on
    // 1. execute proc
    // 2. execute ? := func
    // 3. procedure invocation in a procedure or a function.
    // and stmtKind of these cases are set to QCI_STMT_MASK_SP mask.
    //
    // In this case we are concerened about 3. of the above.
    if ( sStatement->myPlan->parseTree->validate == qsv::validateExeProc )
    {
        sOriSubqueryArgFlag      =
            (sStatement->spvEnv->flag & QSV_ENV_SUBQUERY_ON_ARGU_ALLOW_MASK) ;

        sStatement->spvEnv->flag &= ~QSV_ENV_SUBQUERY_ON_ARGU_ALLOW_MASK ;
        sStatement->spvEnv->flag |= QSV_ENV_SUBQUERY_ON_ARGU_ALLOW_FALSE ;

        sStage = 1;
    }

    IDE_TEST(sStatement->myPlan->parseTree->validate( sStatement ) != IDE_SUCCESS);

    if ( sStatement->myPlan->parseTree->validate == qsv::validateExeProc )
    {
        sStatement->spvEnv->flag &= ~QSV_ENV_SUBQUERY_ON_ARGU_ALLOW_MASK ;
        sStatement->spvEnv->flag |= sOriSubqueryArgFlag ;

        sStage = 0;
    }

    // check INTO clause of SELECT statement
    if ( ( (sStatement->myPlan->parseTree->stmtKind == QCI_STMT_SELECT) ||
           (sStatement->myPlan->parseTree->stmtKind == QCI_STMT_SELECT_FOR_UPDATE) )
         &&
         ( sSQL->isExistsSql == ID_FALSE ) )  // if exists subquery�� �˻����� �ʴ´�.
    {
        sParseTree = (qmsParseTree *) sStatement->myPlan->parseTree;

        IDE_TEST(checkSelectIntoClause(
                     sStatement, aProcStmts, sParseTree->querySet)
                 != IDE_SUCCESS);
    }

    if( sSQL->usingParams != NULL )
    {
        for( sCurrParam = sSQL->usingParams;
             sCurrParam != NULL;
             sCurrParam = sCurrParam->next )
        {
            sCurrParamNode = sCurrParam->paramNode;

            if( sCurrParam->inOutType == QS_IN )
            {
                // Nothing to do.
            }
            else
            {
                // lvalue�� psm������ �����ϹǷ� lvalue flag�� ����.
                // out�� ��쿡�� �ش��. array_index_variable�� ���
                // ������ ������ �ϱ� ����.
                if( sCurrParam->inOutType == QS_OUT )
                {
                    sCurrParamNode->lflag |= QTC_NODE_LVALUE_ENABLE;
                }
                else
                {
                    // Nothing to do.
                }
            }
        }
    }

    // PROJ-1359 Trigger
    // Trigger�� ���� Validation�� �����Ѵ�.
    // Action Body ���� ���� ������ ����� �� ����.
    //     - Transaction Control Statement�� ����� �� ����.
    //     - PSM�� ȣ���� �� ����.
    switch ( aPurpose )
    {
        case QS_PURPOSE_PSM:
            break;
        case QS_PURPOSE_PKG:
            break;
        case QS_PURPOSE_TRIGGER:
            IDE_TEST( checkTriggerActionBody( aStatement, aProcStmts )
                      != IDE_SUCCESS );
            break;
        default:
            IDE_DASSERT(0);
            break;
    }

    if( qsvProcStmts::isSQLType( sSQL->common.stmtType ) == ID_TRUE)
    {
        // BUG-36988
        IDE_TEST( queryTrans( aStatement, aProcStmts )
                  != IDE_SUCCESS );

        // BUG-37655
        // pragma restrict reference
        if( aStatement->spvEnv->createPkg != NULL )
        {
            IDE_TEST( qsvPkgStmts::checkPragmaRestrictReferencesOption(
                          aStatement,
                          aStatement->spvEnv->currSubprogram,
                          aStatement->spvEnv->currStmt,
                          aStatement->spvEnv->isPkgInitializeSection )
                      != IDE_SUCCESS );
        }
        else
        {
            // Nothing to do.
        }

        // BUG-43158 Enhance statement list caching in PSM
        if ( aStatement->spvEnv->createProc != NULL )
        {
            sSQL->sqlIdx = aStatement->spvEnv->createProc->procSqlCount++;
        }
        else
        {
            sSQL->sqlIdx = ID_UINT_MAX;
        }
    }

    IDE_TEST( QC_QME_MEM(aStatement)->free( sStatement )
              != IDE_SUCCESS );
    sSQL->statement = NULL;

    QSV_ENV_INIT_SQL( aStatement );

    return IDE_SUCCESS;

  
    IDE_EXCEPTION_END;

    switch ( sStage )
    {
        case 1 :
            sStatement->spvEnv->flag &= ~QSV_ENV_SUBQUERY_ON_ARGU_ALLOW_MASK ;
            sStatement->spvEnv->flag |= sOriSubqueryArgFlag ;
    }

    // set original error code.
    qsxEnv::setErrorCode( QC_QSX_ENV(aStatement) );

    // BUG-43998
    // PSM ���� ���� �߻��� ���� �߻� ��ġ�� �� ���� ����ϵ��� �մϴ�.
    if ( ideHasErrorPosition() == ID_FALSE )
    {
        sqlInfo.setSourceInfo( aStatement,
                               &aProcStmts->pos );

        (void)sqlInfo.initWithBeforeMessage(
            aStatement->qmeMem );

        IDE_SET(
            ideSetErrorCode(qpERR_ABORT_QSX_SQLTEXT_WRAPPER,
                            sqlInfo.getBeforeErrMessage(),
                            sqlInfo.getErrMessage()));
        (void)sqlInfo.fini();
    }

    // set sophisticated error message.
    qsxEnv::setErrorMessage( QC_QSX_ENV(aStatement) );

    QSV_ENV_INIT_SQL( aStatement );

    return IDE_FAILURE;
}

IDE_RC qsvProcStmts::parseIf(
    qcStatement * aStatement,
    qsProcStmts * aProcStmts)
{
    qsProcStmtIf    * sIF = (qsProcStmtIf *)aProcStmts;
    qsProcStmts     * sProcStmt;

    // condition
    if ( sIF->conditionType == QS_CONDITION_NORMAL )
    {
        if ( sIF->conditionNode != NULL )
        {
            IDE_TEST(qmv::parseViewInExpression( aStatement, sIF->conditionNode )
                     != IDE_SUCCESS);
        }
    }
    else
    {
        sProcStmt = (qsProcStmts*) sIF->existsSql;
        
        IDE_TEST( sProcStmt->parse( aStatement, sProcStmt ) != IDE_SUCCESS );
    }

    // THEN clause
    sProcStmt = sIF->thenStmt;
    if( sProcStmt != NULL )
    {
        IDE_TEST(sProcStmt->parse(aStatement, sProcStmt) != IDE_SUCCESS);
    }
    else
    {
        // Nothing to do.
    }

    // ELSE clause
    sProcStmt = sIF->elseStmt;
    if( sProcStmt != NULL )
    {
        IDE_TEST(sProcStmt->parse(aStatement, sProcStmt) != IDE_SUCCESS);
    }
    else
    {
        // Nothing to do.
    }

    return IDE_SUCCESS;

  
    IDE_EXCEPTION_END;

    return IDE_FAILURE;
}

IDE_RC qsvProcStmts::validateIf( qcStatement     * aStatement,
                                 qsProcStmts     * aProcStmts,
                                 qsProcStmts     * aParentStmt,
                                 qsValidatePurpose aPurpose )
{
    qsProcStmtIf        * sIF = (qsProcStmtIf *)aProcStmts;
    qsProcStmts         * sProcStmt;
    mtcColumn           * sColumn;
    qcuSqlSourceInfo      sqlInfo;

    QS_SET_PARENT_STMT( aProcStmts, aParentStmt );

    aStatement->spvEnv->currStmt = aProcStmts;

    // condtion
    if ( sIF->conditionType == QS_CONDITION_NORMAL )
    {
        if ( sIF->conditionNode != NULL )
        {
            IDE_TEST(qtc::estimate(
                         sIF->conditionNode,
                         QC_SHARED_TMPLATE(aStatement),
                         aStatement,
                         NULL,
                         NULL,
                         NULL )
                     != IDE_SUCCESS);

            sColumn = &(QC_SHARED_TMPLATE(aStatement)->tmplate.
                        rows[sIF->conditionNode->node.table].
                        columns[sIF->conditionNode->node.column]);

            if (sColumn->module != &mtdBoolean)
            {
                sqlInfo.setSourceInfo( aStatement,
                                       &sIF->conditionNode->position );
                IDE_RAISE(ERR_INSUFFICIENT_ARGUEMNT);
            }

            IDE_TEST_RAISE( qsv::checkNoSubquery(
                                aStatement,
                                sIF->conditionNode,
                                & sqlInfo ) != IDE_SUCCESS,
                            ERR_SUBQ_NOT_ALLOWED );
        }
    }
    else
    {
        sProcStmt = (qsProcStmts*) sIF->existsSql;

        IDE_TEST( sProcStmt->validate( aStatement, sProcStmt, aParentStmt, aPurpose )
                  != IDE_SUCCESS );
    }

    // THEN clause
    sProcStmt = sIF->thenStmt;
    if( sProcStmt != NULL )
    {
        IDE_TEST( sProcStmt->validate(aStatement, sProcStmt, aProcStmts, aPurpose)
                  != IDE_SUCCESS);
    }
    else
    {
        // Nothing to do.
    }

    // ELSE clause
    sProcStmt = sIF->elseStmt;
    if( sProcStmt != NULL )
    {
        IDE_TEST(sProcStmt->validate(aStatement, sProcStmt, aProcStmts, aPurpose)
                 != IDE_SUCCESS);
    }
    else
    {
        // Nothing to do.
    }

    return IDE_SUCCESS;

  
    IDE_EXCEPTION(ERR_SUBQ_NOT_ALLOWED);
    {
        (void)sqlInfo.init(aStatement->qmeMem);
        IDE_SET(
            ideSetErrorCode( qpERR_ABORT_QSV_NOT_ALLOWED_SUBQUERY_SQLTEXT,
                             sqlInfo.getErrMessage() ));
        (void)sqlInfo.fini();
    }
    IDE_EXCEPTION(ERR_INSUFFICIENT_ARGUEMNT);
    {
        (void)sqlInfo.init(aStatement->qmeMem);
        IDE_SET(
            ideSetErrorCode( qpERR_ABORT_QSV_INSUFFICIENT_ARGUEMNT_SQLTEXT,
                             sqlInfo.getErrMessage() ));
        (void)sqlInfo.fini();
    }
    IDE_EXCEPTION_END;

    return IDE_FAILURE;
}

IDE_RC qsvProcStmts::parseWhile(
    qcStatement * aStatement,
    qsProcStmts * aProcStmts)
{
    qsProcStmtWhile     * sWHILE = (qsProcStmtWhile *)aProcStmts;

    // condition
    if (sWHILE->conditionNode != NULL)
    {
        IDE_TEST(qmv::parseViewInExpression( aStatement, sWHILE->conditionNode )
                 != IDE_SUCCESS);
    }

    return IDE_SUCCESS;

  
    IDE_EXCEPTION_END;

    return IDE_FAILURE;
}

IDE_RC qsvProcStmts::validateWhile( qcStatement     * aStatement,
                                    qsProcStmts     * aProcStmts,
                                    qsProcStmts     * aParentStmt,
                                    qsValidatePurpose aPurpose )
{
    qsProcStmtWhile     * sWHILE = (qsProcStmtWhile *)aProcStmts;
    qsProcStmts         * sProcStmt;
    qsAllVariables      * sOldAllVariables = NULL;
    mtcColumn           * sColumn;
    qcuSqlSourceInfo      sqlInfo;

    QS_SET_PARENT_STMT( aProcStmts, aParentStmt );

    aStatement->spvEnv->currStmt = aProcStmts;

    if (sWHILE->common.parentLabels != NULL)
    {
        IDE_TEST(connectAllVariables(
                     aStatement,
                     sWHILE->common.parentLabels, NULL, ID_TRUE,
                     &sOldAllVariables)
                 != IDE_SUCCESS);
    }

    // BUG-41262 PSM overloading
    // PSM overloading �� parser �ܰ迡�� ó���Ѵ�.
    // ������ PSM ������ ������ connectAllVariables �� ȣ���Ŀ� ������ �����ϴ�.
    for (sProcStmt = sWHILE->loopStmts;
         sProcStmt != NULL;
         sProcStmt = sProcStmt->next)
    {
        IDE_TEST(sProcStmt->parse(aStatement, sProcStmt) != IDE_SUCCESS);
    }

    if (sWHILE->conditionNode != NULL)
    {
        IDE_TEST(qtc::estimate(
                     sWHILE->conditionNode,
                     QC_SHARED_TMPLATE(aStatement),
                     aStatement,
                     NULL,
                     NULL,
                     NULL )
                 != IDE_SUCCESS);

        sColumn = &(QC_SHARED_TMPLATE(aStatement)->tmplate.
                    rows[sWHILE->conditionNode->node.table].
                    columns[sWHILE->conditionNode->node.column]);

        if (sColumn->module != &mtdBoolean)
        {
            sqlInfo.setSourceInfo( aStatement,
                                   &sWHILE->conditionNode->position );
            IDE_RAISE(ERR_INSUFFICIENT_ARGUEMNT);
        }

        IDE_TEST_RAISE (qsv::checkNoSubquery(
                            aStatement,
                            sWHILE->conditionNode,
                            & sqlInfo ) != IDE_SUCCESS,
                        ERR_SUBQ_NOT_ALLOWED );
    }

    // loop statements
    (aStatement->spvEnv->loopCount)++;

    for (sProcStmt = sWHILE->loopStmts;
         sProcStmt != NULL;
         sProcStmt = sProcStmt->next)
    {
        IDE_TEST( sProcStmt->validate(aStatement, sProcStmt, aProcStmts, aPurpose )
                  != IDE_SUCCESS);
    }

    (aStatement->spvEnv->loopCount)--;

    if (sWHILE->common.parentLabels != NULL)
    {
        disconnectAllVariables( aStatement, sOldAllVariables );
    }

    return IDE_SUCCESS;

  
    IDE_EXCEPTION(ERR_SUBQ_NOT_ALLOWED);
    {
        (void)sqlInfo.init(aStatement->qmeMem);
        IDE_SET(
            ideSetErrorCode( qpERR_ABORT_QSV_NOT_ALLOWED_SUBQUERY_SQLTEXT,
                             sqlInfo.getErrMessage() ));
        (void)sqlInfo.fini();
    }
    IDE_EXCEPTION(ERR_INSUFFICIENT_ARGUEMNT);
    {
        (void)sqlInfo.init(aStatement->qmeMem);
        IDE_SET(
            ideSetErrorCode( qpERR_ABORT_QSV_INSUFFICIENT_ARGUEMNT_SQLTEXT,
                             sqlInfo.getErrMessage() ));
        (void)sqlInfo.fini();
    }
    IDE_EXCEPTION_END;

    return IDE_FAILURE;
}

IDE_RC qsvProcStmts::parseFor(
    qcStatement * aStatement,
    qsProcStmts * aProcStmts)
{
    qsProcStmtFor      * sFOR = (qsProcStmtFor *)aProcStmts;

    // lower
    if (sFOR->lowerNode != NULL)
    {
        IDE_TEST(qmv::parseViewInExpression(aStatement, sFOR->lowerNode)
                 != IDE_SUCCESS);
    }

    // upper
    if (sFOR->upperNode != NULL)
    {
        IDE_TEST(qmv::parseViewInExpression(aStatement, sFOR->upperNode)
                 != IDE_SUCCESS);
    }

    // step
    if (sFOR->stepNode != NULL)
    {
        IDE_TEST(qmv::parseViewInExpression(aStatement, sFOR->stepNode)
                 != IDE_SUCCESS);
    }

    return IDE_SUCCESS;

  
    IDE_EXCEPTION_END;

    return IDE_FAILURE;
}

IDE_RC qsvProcStmts::validateFor( qcStatement     * aStatement,
                                  qsProcStmts     * aProcStmts,
                                  qsProcStmts     * aParentStmt,
                                  qsValidatePurpose aPurpose )
{
    qsProcStmtFor      * sFOR = (qsProcStmtFor *)aProcStmts;
    qsProcStmts        * sProcStmt;
    qsVariables        * sVariable;
    qsAllVariables     * sOldAllVariables;
    qcuSqlSourceInfo     sqlInfo;


    QS_SET_PARENT_STMT( aProcStmts, aParentStmt );

    aStatement->spvEnv->currStmt = aProcStmts;

    // validate counter variable
    //  (1) counter cannot be specified with label name.
    //      label_name.counter_name : illegal
    //      counter_name            : legal
    //  (2) counter variable is read-only variable.
    //  (3) It do not need that
    //      a counter variable is a declared local variable in declare section.

    // make qsVariableItems for counter variable
    IDE_TEST(STRUCT_ALLOC(QC_QME_MEM(aStatement), qsVariables, &sVariable)
             != IDE_SUCCESS);

    sVariable->common.itemType  = QS_VARIABLE;
    SET_POSITION( sVariable->common.name,
                  sFOR->counterVar->columnName );
    sVariable->common.table     = sFOR->counterVar->node.table;
    sVariable->common.column    = sFOR->counterVar->node.column;
    sVariable->common.objectID  = sFOR->counterVar->node.objectID; 
    sVariable->common.next      = NULL;
    sVariable->variableTypeNode = sFOR->counterVar;
    sVariable->variableType     = QS_PRIM_TYPE;
    sVariable->defaultValueNode = NULL;
    sVariable->inOutType        = QS_IN;    // read-only variable
    sVariable->typeInfo         = NULL;

    IDE_TEST(connectAllVariables(
                 aStatement,
                 sFOR->common.parentLabels, (qsVariableItems *)sVariable, ID_TRUE,
                 &sOldAllVariables)
             != IDE_SUCCESS);

    // BUG-41262 PSM overloading
    // PSM overloading �� parser �ܰ迡�� ó���Ѵ�.
    // ������ PSM ������ ������ connectAllVariables �� ȣ���Ŀ� ������ �����ϴ�.
    for (sProcStmt = sFOR->loopStmts;
         sProcStmt != NULL;
         sProcStmt = sProcStmt->next)
    {
        IDE_TEST(sProcStmt->parse(aStatement, sProcStmt) != IDE_SUCCESS);
    }

    // validate lower bound expression
    if (sFOR->lowerNode != NULL)
    {
        IDE_TEST(qtc::estimate(
                     sFOR->lowerNode,
                     QC_SHARED_TMPLATE(aStatement),
                     aStatement,
                     NULL,
                     NULL,
                     NULL )
                 != IDE_SUCCESS);

        IDE_TEST_RAISE( qsv::checkNoSubquery(
                            aStatement,
                            sFOR->lowerNode,
                            & sqlInfo ) != IDE_SUCCESS,
                        ERR_SUBQ_NOT_ALLOWED );

        IDE_TEST( checkIndexVariableOfFor( aStatement,
                                           sFOR->counterVar,
                                           sFOR->lowerNode)
                  != IDE_SUCCESS);
    }

    // validate upper bound expression
    if (sFOR->upperNode != NULL)
    {
        IDE_TEST(qtc::estimate(
                     sFOR->upperNode,
                     QC_SHARED_TMPLATE(aStatement),
                     aStatement,
                     NULL,
                     NULL,
                     NULL )
                 != IDE_SUCCESS);

        IDE_TEST_RAISE( qsv::checkNoSubquery(
                            aStatement,
                            sFOR->upperNode,
                            & sqlInfo ) != IDE_SUCCESS,
                        ERR_SUBQ_NOT_ALLOWED );

        IDE_TEST( checkIndexVariableOfFor( aStatement,
                                           sFOR->counterVar,
                                           sFOR->upperNode)
                  != IDE_SUCCESS);
    }

    // validate step size expression
    if (sFOR->stepNode != NULL)
    {
        IDE_TEST(qtc::estimate(
                     sFOR->stepNode,
                     QC_SHARED_TMPLATE(aStatement),
                     aStatement,
                     NULL,
                     NULL,
                     NULL )
                 != IDE_SUCCESS);

        IDE_TEST_RAISE( qsv::checkNoSubquery(
                            aStatement,
                            sFOR->stepNode,
                            & sqlInfo ) != IDE_SUCCESS,
                        ERR_SUBQ_NOT_ALLOWED );
    }

    // validate ( stepNode > 0 )
    if (sFOR->isStepOkNode != NULL)
    {
        IDE_TEST(qtc::estimate(
                     sFOR->isStepOkNode,
                     QC_SHARED_TMPLATE(aStatement),
                     aStatement,
                     NULL,
                     NULL,
                     NULL )
                 != IDE_SUCCESS);
    }

    // validate ( lowerNode <= upperNode )
    if (sFOR->isIntervalOkNode != NULL)
    {
        IDE_TEST(qtc::estimate(
                     sFOR->isIntervalOkNode,
                     QC_SHARED_TMPLATE(aStatement),
                     aStatement,
                     NULL,
                     NULL,
                     NULL )
                 != IDE_SUCCESS);

        // To Fix PR-8816
        // FOR i1 IN 1 .. 100 LOOP
        // ���� ���� FOR LOOP ������ lowerNode(1)�� upperNode(100)��
        // Interval�� ��ȿ�� �˻�( lowerNode <= upperNode )�� ���� ó������
        // ����ȯ�� �߻��� �� �ִ�.
        // �̷��� ����ȯ�� Parsing �ܰ迡�� 1 �� ���� ������ SMALLINT��
        // ����ϱ� ������ �߻��ϴ� �����̸�, INTEGER�� ����ȯ�� Node��
        // lowerNode�� upperNode�� ��ġ�Ͽ��� �Ѵ�.
        // Parsing �ܰ迡�� INTEGER�� ������ �� ���� ������
        // FOR �������� Expression�� �Ϲ� Expression�� �����ϴ� ��
        // ��ü�� ����ġ�� ������ Parsing ������ ���߽�Ű�� �����̴�.

        // ���� TO_INTEGER() �� ���� ����ȯ �Լ��� �����Ǹ�,
        // �̴� Parsing �ܰ迡�� ó���� �� �ִ�.

        // To Fix PR-11391 �񱳸� lowerVar, upperVar�� �ϱ� ������
        // conversion�� �Ͼ�� �ʴ´�.
        //sFOR->lowerNode = (qtcNode *) sFOR->isIntervalOkNode->node.arguments;
        //sFOR->upperNode =
        //    (qtcNode *) sFOR->isIntervalOkNode->node.arguments->next;
    }

    // validate ( counterVar BETWEEN lowerNode AND upperNode )
    if (sFOR->conditionNode != NULL)
    {
        IDE_TEST(qtc::estimate(
                     sFOR->conditionNode,
                     QC_SHARED_TMPLATE(aStatement),
                     aStatement,
                     NULL,
                     NULL,
                     NULL )
                 != IDE_SUCCESS);
    }

    // validate ( counterVar + 1 )
    if (sFOR->newCounterNode != NULL)
    {
        IDE_TEST(qtc::estimate(
                     sFOR->newCounterNode,
                     QC_SHARED_TMPLATE(aStatement),
                     aStatement,
                     NULL,
                     NULL,
                     NULL )
                 != IDE_SUCCESS);
    }

    // loop statements
    (aStatement->spvEnv->loopCount)++;

    for (sProcStmt = sFOR->loopStmts;
         sProcStmt != NULL;
         sProcStmt = sProcStmt->next)
    {
        IDE_TEST( sProcStmt->validate( aStatement, sProcStmt, aProcStmts, aPurpose )
                  != IDE_SUCCESS);
    }

    (aStatement->spvEnv->loopCount)--;

    disconnectAllVariables( aStatement, sOldAllVariables );

    return IDE_SUCCESS;

  
    IDE_EXCEPTION(ERR_SUBQ_NOT_ALLOWED);
    {
        (void)sqlInfo.init(aStatement->qmeMem);
        IDE_SET(
            ideSetErrorCode( qpERR_ABORT_QSV_NOT_ALLOWED_SUBQUERY_SQLTEXT,
                             sqlInfo.getErrMessage() ));
        (void)sqlInfo.fini();
    }
    IDE_EXCEPTION_END;

    return IDE_FAILURE;
}


IDE_RC qsvProcStmts::checkIndexVariableOfFor(
    qcStatement * aStatement,
    qtcNode     * aCounter,
    qtcNode     * aBound)
{
    qtcNode            * sNode;
    qcuSqlSourceInfo     sqlInfo;

    for ( sNode = (qtcNode *)(aBound->node.arguments);
          sNode != NULL;
          sNode = (qtcNode *)(sNode->node.next))
    {
        IDE_TEST( checkIndexVariableOfFor(aStatement, aCounter, sNode)
                  != IDE_SUCCESS);
    }

    if ( ( aCounter->node.table == aBound->node.table ) &&
         ( aCounter->node.column == aBound->node.column ) &&
         ( aCounter->node.objectID == aBound->node.objectID ) )
    {
        sqlInfo.setSourceInfo( aStatement,
                               &aBound->columnName);
        IDE_RAISE(ERR_COUNTER_VAR_NOT_ALLOWED);
    }

    return IDE_SUCCESS;

  
    IDE_EXCEPTION(ERR_COUNTER_VAR_NOT_ALLOWED);
    {
        (void)sqlInfo.init(aStatement->qmeMem);
        IDE_SET(
            ideSetErrorCode( qpERR_ABORT_QSV_NOT_ALLOWED_COUNTER_VAR_SQLTEXT,
                             sqlInfo.getErrMessage() ));
        (void)sqlInfo.fini();
    }
    IDE_EXCEPTION_END;

    return IDE_FAILURE;
}


IDE_RC qsvProcStmts::parseExit(
    qcStatement * aStatement,
    qsProcStmts * aProcStmts)
{
    qsProcStmtExit      * sEXIT = (qsProcStmtExit *)aProcStmts;

    // condition
    if (sEXIT->conditionNode != NULL)
    {
        IDE_TEST(qmv::parseViewInExpression( aStatement, sEXIT->conditionNode )
                 != IDE_SUCCESS);
    }

    return IDE_SUCCESS;

  
    IDE_EXCEPTION_END;

    return IDE_FAILURE;
}


IDE_RC qsvProcStmts::validateExit( qcStatement     * aStatement,
                                   qsProcStmts     * aProcStmts,
                                   qsProcStmts     * aParentStmt,
                                   qsValidatePurpose /* aPurpose */ )
{
    qsProcStmtExit      * sEXIT = (qsProcStmtExit *)aProcStmts;
    qsAllVariables      * sCurrVar;
    qsLabels            * sLabel;
    mtcColumn           * sColumn;
    idBool                sFind     = ID_FALSE;
    qcuSqlSourceInfo      sqlInfo;

    QS_SET_PARENT_STMT( aProcStmts, aParentStmt );

    aStatement->spvEnv->currStmt = aProcStmts;

    if (aStatement->spvEnv->loopCount <= 0)
    {
        sqlInfo.setSourceInfo(
            aStatement,
            &sEXIT->common.pos );
        IDE_RAISE(ERR_NOT_USE_IN_LOOP);
    }
    else
    {
        // Nothing to do.
    }

    // BUG-27442
    // Validate length of Label name
    if ( sEXIT->labelNamePos.size > QC_MAX_OBJECT_NAME_LEN )
    {
        sqlInfo.setSourceInfo(
            aStatement,
            &sEXIT->labelNamePos );
        IDE_RAISE( ERR_MAX_NAME_LEN_OVERFLOW );
    }
    else
    {
        // Nothing to do.
    }

    // check label name
    if (QC_IS_NULL_NAME(sEXIT->labelNamePos) == ID_FALSE)
    {
        for (sCurrVar = aStatement->spvEnv->allVariables;
             sCurrVar != NULL && sFind == ID_FALSE;
             sCurrVar = sCurrVar->next)
        {
            if (sCurrVar->inLoop == ID_TRUE)
            {
                for (sLabel = sCurrVar->labels;
                     sLabel != NULL && sFind == ID_FALSE;
                     sLabel = sLabel->next)
                {
                    if (idlOS::strMatch(
                            sLabel->namePos.stmtText + sLabel->namePos.offset,
                            sLabel->namePos.size,
                            sEXIT->labelNamePos.stmtText + sEXIT->labelNamePos.offset,
                            sEXIT->labelNamePos.size) == 0)
                    {
                        if (sLabel->stmt == NULL)
                        {
                            sqlInfo.setSourceInfo(
                                aStatement,
                                &sEXIT->common.pos );
                            IDE_RAISE(ERR_NOT_LOOP_LABEL);
                        }

                        sEXIT->labelID = sLabel->id;
                        sEXIT->stmt    = sLabel->stmt;

                        sFind = ID_TRUE;
                        break;
                    }
                }
            }
        }

        if (sFind == ID_FALSE)
        {
            sqlInfo.setSourceInfo(
                aStatement,
                &sEXIT->common.pos );
            IDE_RAISE(ERR_NOT_LOOP_LABEL);
        }
    }
    else
    {
        sEXIT->labelID = QS_ID_INIT_VALUE ;
        sEXIT->stmt    = NULL;
    }

    // validate condition
    if (sEXIT->conditionNode != NULL)
    {
        IDE_TEST(qtc::estimate(
                     sEXIT->conditionNode,
                     QC_SHARED_TMPLATE(aStatement),
                     aStatement,
                     NULL,
                     NULL,
                     NULL )
                 != IDE_SUCCESS);

        sColumn = &(QC_SHARED_TMPLATE(aStatement)->tmplate.
                    rows[sEXIT->conditionNode->node.table].
                    columns[sEXIT->conditionNode->node.column]);

        if (sColumn->module != &mtdBoolean)
        {
            sqlInfo.setSourceInfo( aStatement,
                                   &sEXIT->conditionNode->position );
            IDE_RAISE(ERR_INSUFFICIENT_ARGUEMNT);
        }

        IDE_TEST_RAISE( qsv::checkNoSubquery(
                            aStatement,
                            sEXIT->conditionNode,
                            & sqlInfo ) != IDE_SUCCESS,
                        ERR_SUBQ_NOT_ALLOWED );
    }

    return IDE_SUCCESS;

  
    IDE_EXCEPTION(ERR_SUBQ_NOT_ALLOWED);
    {
        (void)sqlInfo.init(aStatement->qmeMem);
        IDE_SET(
            ideSetErrorCode( qpERR_ABORT_QSV_NOT_ALLOWED_SUBQUERY_SQLTEXT,
                             sqlInfo.getErrMessage() ));
        (void)sqlInfo.fini();
    }
    IDE_EXCEPTION(ERR_NOT_USE_IN_LOOP);
    {
        (void)sqlInfo.init(aStatement->qmeMem);
        IDE_SET(
            ideSetErrorCode(qpERR_ABORT_QSV_PROC_NOT_USE_IN_LOOP_SQLTEXT,
                            sqlInfo.getErrMessage() ));
        (void)sqlInfo.fini();
    }
    IDE_EXCEPTION(ERR_NOT_LOOP_LABEL);
    {
        (void)sqlInfo.init(aStatement->qmeMem);
        IDE_SET(
            ideSetErrorCode(qpERR_ABORT_QSV_PROC_NOT_LOOP_LABEL_SQLTEXT,
                            sqlInfo.getErrMessage() ));
        (void)sqlInfo.fini();
    }
    IDE_EXCEPTION(ERR_INSUFFICIENT_ARGUEMNT);
    {
        (void)sqlInfo.init(aStatement->qmeMem);
        IDE_SET(
            ideSetErrorCode( qpERR_ABORT_QSV_INSUFFICIENT_ARGUEMNT_SQLTEXT,
                             sqlInfo.getErrMessage() ));
        (void)sqlInfo.fini();
    }
    IDE_EXCEPTION(ERR_MAX_NAME_LEN_OVERFLOW)
    {
        (void)sqlInfo.init(aStatement->qmeMem);
        IDE_SET(
            ideSetErrorCode(qpERR_ABORT_QCP_MAX_NAME_LENGTH_OVERFLOW,
                            sqlInfo.getErrMessage() ));
        (void)sqlInfo.fini();
    }
    IDE_EXCEPTION_END;

    return IDE_FAILURE;
}

IDE_RC qsvProcStmts::validateContinue( qcStatement     * aStatement,
                                       qsProcStmts     * aProcStmts,
                                       qsProcStmts     * aParentStmt,
                                       qsValidatePurpose /* aPurpose */ )
{
    qsProcStmtContinue * sCONTINUE  = (qsProcStmtContinue *)aProcStmts;
    qcuSqlSourceInfo     sqlInfo;

    QS_SET_PARENT_STMT( aProcStmts, aParentStmt );

    aStatement->spvEnv->currStmt = aProcStmts;

    if (aStatement->spvEnv->loopCount <= 0)
    {
        sqlInfo.setSourceInfo(
            aStatement,
            &sCONTINUE->common.pos );
        IDE_RAISE(ERR_NOT_USE_IN_LOOP);
    }

    return IDE_SUCCESS;

  
    IDE_EXCEPTION(ERR_NOT_USE_IN_LOOP);
    {
        (void)sqlInfo.init(aStatement->qmeMem);
        IDE_SET(
            ideSetErrorCode(qpERR_ABORT_QSV_PROC_NOT_USE_IN_LOOP_SQLTEXT,
                            sqlInfo.getErrMessage() ));
        (void)sqlInfo.fini();
    }
    IDE_EXCEPTION_END;
    
    return IDE_FAILURE;
}

IDE_RC qsvProcStmts::validateGoto( qcStatement     * aStatement,
                                   qsProcStmts     * aProcStmts,
                                   qsProcStmts     * aParentStmt,
                                   qsValidatePurpose /* aPurpose */ )
{
    qsProcStmtGoto      * sGOTO = (qsProcStmtGoto *)aProcStmts;
    qcuSqlSourceInfo      sqlInfo;

    QS_SET_PARENT_STMT( aProcStmts, aParentStmt );

    aStatement->spvEnv->currStmt = aProcStmts;

    // GOTO�� Validation�� Optimize�ܰ迡�� �Ѵ�.

    // BUG-27442
    // Validate length of Label name
    if ( sGOTO->labelNamePos.size > QC_MAX_OBJECT_NAME_LEN )
    {
        sqlInfo.setSourceInfo(
            aStatement,
            &sGOTO->labelNamePos );
        IDE_RAISE( ERR_MAX_NAME_LEN_OVERFLOW );
    }
    else
    {
        // Nothing to do.
    }

    return IDE_SUCCESS;

  
    IDE_EXCEPTION(ERR_MAX_NAME_LEN_OVERFLOW)
    {
        (void)sqlInfo.init(aStatement->qmeMem);
        IDE_SET(
            ideSetErrorCode(qpERR_ABORT_QCP_MAX_NAME_LENGTH_OVERFLOW,
                            sqlInfo.getErrMessage() ));
        (void)sqlInfo.fini();
    }
    IDE_EXCEPTION_END;

    return IDE_FAILURE;
}

IDE_RC qsvProcStmts::validateExecImm( qcStatement     * aStatement,
                                      qsProcStmts     * aProcStmts,
                                      qsProcStmts     * aParentStmt,
                                      qsValidatePurpose /* aPurpose */ )
{
    qsProcStmtExecImm * sExecImm = (qsProcStmtExecImm *)aProcStmts;
    qcuSqlSourceInfo    sqlInfo;
    mtcColumn         * sMtcColumn;
    idBool              sFindVar;
    idBool              sExistsRecordVar;
    qsVariables       * sArrayVariable;
    qsUsingParam      * sCurrParam;
    qtcNode           * sCurrParamNode;

    QS_SET_PARENT_STMT( aProcStmts, aParentStmt );
    
    aStatement->spvEnv->currStmt = aProcStmts;

    sFindVar         = ID_FALSE;
    sExistsRecordVar = ID_FALSE;
    
    // 0. query string�� ������Ÿ���� varchar�� char�� �Ǿ�� ��.
    IDE_TEST( qtc::estimate( sExecImm->sqlStringNode,
                             QC_SHARED_TMPLATE(aStatement),
                             aStatement,
                             NULL,
                             NULL,
                             NULL )
              != IDE_SUCCESS );
 
    sMtcColumn = QTC_STMT_COLUMN( aStatement,
                                  sExecImm->sqlStringNode );

    if( ( sMtcColumn->module != &mtdVarchar ) &&
        ( sMtcColumn->module != &mtdChar ) )
    {
        sqlInfo.setSourceInfo( aStatement,
                               &sExecImm->sqlStringNode->position );
        IDE_RAISE(ERR_INSUFFICIENT_ARGUEMNT);
    }
    else
    {
        // Nothing to do.
    }

    IDE_TEST_RAISE( qsv::checkNoSubquery(
                        aStatement,
                        sExecImm->sqlStringNode,
                        & sqlInfo ) != IDE_SUCCESS,
                    ERR_SUBQ_NOT_ALLOWED );
    
    // 1. into���� �ִٸ� into���� ���� validation
    if( sExecImm->intoVariableNodes != NULL )
    {
        // 1.1 into���� ��� procedure������ ����.
        // execute immediate���� target�� ������ �� �� ����.
        IDE_TEST( validateIntoClauseInternal( aStatement,
                                              sExecImm->intoVariableNodes,
                                              0, /* target count */
                                              &sExistsRecordVar,
                                              ID_TRUE, /* aIsExecImm */
                                              ID_FALSE, /* alsRefCur */
                                              NULL /* aIntoVarCount */ )
                  != IDE_SUCCESS );

        if ( sExistsRecordVar == ID_TRUE )
        {
            sExecImm->isIntoVarRecordType = ID_TRUE;
        }
        else
        {
            sExecImm->isIntoVarRecordType = ID_FALSE;
        }
    } /* sExecImm->intoVariableNodes != NULL */
    else
    {
        // Nothing to do.
    }

    sExecImm->usingParamCount = 0;
    
    // 2. using���� �ִٸ� using���� ���� validation
    if( sExecImm->usingParams != NULL )
    {
        for( sCurrParam = sExecImm->usingParams;
             sCurrParam != NULL;
             sCurrParam = sCurrParam->next )
        {
            sCurrParamNode = sCurrParam->paramNode;

            if( sCurrParam->inOutType == QS_IN )
            {
                // 2.1 inŸ���� ��� Ư���� �˻縦 ���� ����.
                IDE_TEST( qtc::estimate( sCurrParamNode,
                                         QC_SHARED_TMPLATE(aStatement), 
                                         aStatement,
                                         NULL,
                                         NULL,
                                         NULL )
                      != IDE_SUCCESS );
            }
            else
            {
                // 2.2 out, in out�� ��� �ݵ�� procedure�������� ��.
                // invalid out argument ������ ��.
                IDE_TEST(qsvProcVar::searchVarAndPara(
                         aStatement,
                         sCurrParamNode,
                         ID_TRUE, // for OUTPUT
                         &sFindVar,
                         &sArrayVariable)
                     != IDE_SUCCESS);

                // PROJ-1073 Package
                if( sFindVar == ID_FALSE )
                {
                    IDE_TEST( qsvProcVar::searchVariableFromPkg(
                            aStatement,
                            sCurrParamNode,
                            &sFindVar,
                            &sArrayVariable )
                        != IDE_SUCCESS )
                }
            
                if (sFindVar == ID_FALSE)
                {
                    sqlInfo.setSourceInfo(
                        aStatement,
                        &sCurrParamNode->position );
                    IDE_RAISE(ERR_INVALID_OUT_ARGUMENT);
                }
                else
                {
                    // Nothing to do.
                }

                // lvalue�� psm������ �����ϹǷ� lvalue flag�� ����.
                // out�� ��쿡�� �ش��. array_index_variable�� ���
                // ������ ������ �ϱ� ����.
                if( sCurrParam->inOutType == QS_OUT )
                {
                    sCurrParamNode->lflag |= QTC_NODE_LVALUE_ENABLE;
                }
                else
                {
                    // Nothing to do.
                }

                // 2.3 out, in out�� ��� outbinding_disable�̸� ����

                IDE_TEST( qtc::estimate( sCurrParamNode,
                                         QC_SHARED_TMPLATE(aStatement), 
                                         aStatement,
                                         NULL,
                                         NULL,
                                         NULL )
                          != IDE_SUCCESS );

                // BUG-42790 lvalue���� �׻� columnModule�̿����Ѵ�.
                IDE_ERROR_RAISE( ( sCurrParamNode->node.module ==
                                   &qtc::columnModule ),
                                 ERR_UNEXPECTED_MODULE_ERROR );

                if ( ( sCurrParamNode->lflag & QTC_NODE_OUTBINDING_MASK )
                     == QTC_NODE_OUTBINDING_DISABLE )
                {
                    sqlInfo.setSourceInfo(
                        aStatement,
                        &sCurrParamNode->position );
                    IDE_RAISE(ERR_INVALID_OUT_ARGUMENT);
                }
                else
                {
                    // Nothing to do.
                }
            }

            sExecImm->usingParamCount++;
        } // end for
    }
    else
    {
        // Nothing to do.
    }
    
    return IDE_SUCCESS;

  
    IDE_EXCEPTION(ERR_SUBQ_NOT_ALLOWED);
    {
        (void)sqlInfo.init(aStatement->qmeMem);
        IDE_SET(
            ideSetErrorCode( qpERR_ABORT_QSV_NOT_ALLOWED_SUBQUERY_SQLTEXT,
                             sqlInfo.getErrMessage() ));
        (void)sqlInfo.fini();
    }
    IDE_EXCEPTION(ERR_INSUFFICIENT_ARGUEMNT);
    {
        (void)sqlInfo.init(aStatement->qmeMem);
        IDE_SET(
            ideSetErrorCode( qpERR_ABORT_QSV_INSUFFICIENT_ARGUEMNT_SQLTEXT,
                             sqlInfo.getErrMessage() ));
        (void)sqlInfo.fini();
    }
    IDE_EXCEPTION(ERR_INVALID_OUT_ARGUMENT);
    {
        (void)sqlInfo.init(aStatement->qmeMem);
        IDE_SET(
            ideSetErrorCode(qpERR_ABORT_QSV_INVALID_OUT_ARGUEMNT_SQLTEXT,
                            sqlInfo.getErrMessage() ));
        (void)sqlInfo.fini();
    }
    IDE_EXCEPTION( ERR_UNEXPECTED_MODULE_ERROR )
    {
        IDE_SET( ideSetErrorCode( qpERR_ABORT_QMC_UNEXPECTED_ERROR,
                                  "qsvProcStmts::validateExecImm",
                                  "The module is unexpected" ) );
    }
    IDE_EXCEPTION_END;

    return IDE_FAILURE;
}

IDE_RC qsvProcStmts::validateOpenFor( qcStatement     * aStatement,
                                      qsProcStmts     * aProcStmts,
                                      qsProcStmts     * aParentStmt,
                                      qsValidatePurpose aPurpose )
{
/***********************************************************************
 *
 *  Description : PROJ-1386 Dynamic-SQL
 *                OPEN ... FOR ... ������ ���� validation
 *
 *  Implementation :
 *
 ***********************************************************************/
    
    qsProcStmtOpenFor * sOpenFor = (qsProcStmtOpenFor *)aProcStmts;
    idBool              sFindVar;
    qcuSqlSourceInfo    sqlInfo;    
    qsProcStmtBlock   * sBlock;
    qsVariables       * sVariables;
    
    QS_SET_PARENT_STMT( aProcStmts, aParentStmt );
    
    aStatement->spvEnv->currStmt = aProcStmts;

    IDE_TEST( qsvRefCursor::searchRefCursorVar(
                  aStatement,
                  sOpenFor->refCursorVarNode,
                  &sVariables,
                  &sFindVar )
              != IDE_SUCCESS );

    if( sFindVar == ID_FALSE )
    {
        sqlInfo.setSourceInfo(
            aStatement,
            &sOpenFor->refCursorVarNode->position );
        IDE_RAISE(ERR_NOT_FOUND_VAR);
    }
    else
    {
        /* BUG-38509 autonomous transaction
           autonomous transaction block���� ref cursor�� ������� �� �Ѵ�.
           ��� �� execute�� �� fatal �߻� */
        if ( aStatement->spvEnv->createProc != NULL )
        {
            sBlock = aStatement->spvEnv->createProc->block;

            IDE_DASSERT( sBlock != NULL );

            if ( sBlock->isAutonomousTransBlock == ID_TRUE )
            {
                sqlInfo.setSourceInfo(
                    aStatement,
                    &sOpenFor->refCursorVarNode->position );
                IDE_RAISE(ERR_CANNOT_USE_IN_AUTONOMOUSTRANS_BLOCK);
            }
        }
        else
        {
            // Nothing to do.
        }
    }

    // BUG-38767
    if( sOpenFor->refCursorVarNode->node.objectID == 0 )
    {
        sOpenFor->sqlIdx = sVariables->resIdx;
    }
    else
    {
        // package spec�� ������ cursor�̸� sqlIdx�� ���� �Ҵ��Ѵ�.
        // BUG-43158 Enhance statement list caching in PSM
        if ( aStatement->spvEnv->createProc != NULL )
        {
            sOpenFor->sqlIdx = aStatement->spvEnv->createProc->procSqlCount++;
        }
        else
        {
            sOpenFor->sqlIdx = ID_UINT_MAX;
        }
    }

    if ( ( sOpenFor->refCursorVarNode->lflag & QTC_NODE_OUTBINDING_MASK )
         == QTC_NODE_OUTBINDING_DISABLE )
    {
        sqlInfo.setSourceInfo(
            aStatement,
            &sOpenFor->refCursorVarNode->position );
        IDE_RAISE(ERR_INOUT_TYPE_MISMATCH);
    }
    else
    {
        // Nothing to do.
    }

    if ( sOpenFor->common.stmtType == QS_PROC_STMT_OPEN_FOR )
    {
        IDE_TEST( qsvRefCursor::validateOpenFor( aStatement,
                                                 sOpenFor )
                  != IDE_SUCCESS );              
    }
    else
    {
        // BUG-42397 Ref Cursor Static SQL
        IDE_DASSERT( sOpenFor->common.stmtType == QS_PROC_STMT_OPEN_FOR_STATIC_SQL );

        IDE_TEST( parseSql( aStatement,
                            (qsProcStmts*)sOpenFor->staticSql )
                  != IDE_SUCCESS );

        IDE_TEST( validateSql( aStatement,
                               (qsProcStmts*)sOpenFor->staticSql,
                               aParentStmt,
                               aPurpose )
                  != IDE_SUCCESS );

        sOpenFor->usingParams     = sOpenFor->staticSql->usingParams;
        sOpenFor->usingParamCount = sOpenFor->staticSql->usingParamCount;
    }

    return IDE_SUCCESS;

  
    IDE_EXCEPTION(ERR_NOT_FOUND_VAR);
    {   
        (void)sqlInfo.init(aStatement->qmeMem);
        IDE_SET(
            ideSetErrorCode(qpERR_ABORT_QSV_NOT_EXIST_VARIABLE_NAME_SQLTEXT,
                            sqlInfo.getErrMessage() ));
        (void)sqlInfo.fini();
    }
    IDE_EXCEPTION(ERR_INOUT_TYPE_MISMATCH);
    {
        (void)sqlInfo.init(aStatement->qmeMem);
        IDE_SET(
            ideSetErrorCode(
                qpERR_ABORT_QSV_PROC_ASSIGN_LVALUE_NO_READONLY_VAR_SQLTEXT,
                sqlInfo.getErrMessage() ));
        (void)sqlInfo.fini();
    }
    IDE_EXCEPTION( ERR_CANNOT_USE_IN_AUTONOMOUSTRANS_BLOCK )
    {
        (void)sqlInfo.init(aStatement->qmeMem);
        IDE_SET(
            ideSetErrorCode( qpERR_ABORT_QSV_CANNOT_USE_IN_AUTONOMOUS_TRANSACTION,
                             sqlInfo.getErrMessage() ));
        (void)sqlInfo.fini();
    }
    IDE_EXCEPTION_END;

    return IDE_FAILURE;
}

IDE_RC qsvProcStmts::validateFetch( qcStatement     * aStatement,
                                    qsProcStmts     * aProcStmts,
                                    qsProcStmts     * aParentStmt,
                                    qsValidatePurpose aPurpose )
{
/***********************************************************************
 *
 *  Description : PROJ-1386 Dynamic-SQL
 *
 *
 *  Implementation :
 *         (1) ref cursor variable�� ���� ������, cursor�� ���� ������ �з�
 *         (2) �з��� ����� qsProcStmtFetch����ü�� ����
 *         (3) ������ �´� validate�Լ��� ȣ��
 *
 ***********************************************************************/

    idBool             sIsFound;
    qcuSqlSourceInfo   sqlInfo;
    qsProcStmtFetch  * sProcStmtFetch = (qsProcStmtFetch*)aProcStmts;
    qsCursors        * sCursorDef;
    qsProcStmtBlock  * sBlock;
    qsVariables      * sVariables;

    QS_SET_PARENT_STMT( aProcStmts, aParentStmt );
    
    aStatement->spvEnv->currStmt = aProcStmts;

    IDE_TEST( qsvRefCursor::searchRefCursorVar(
                  aStatement,
                  sProcStmtFetch->cursorNode,
                  &sVariables,
                  &sIsFound )
              != IDE_SUCCESS );

    if( sIsFound == ID_TRUE )
    {
        sProcStmtFetch->isRefCursor = ID_TRUE;

        /* BUG-38509 autonomous transaction
           autonomous transaction block���� ref cursor�� ������� �� �Ѵ�.
           ��� �� execute�� �� fatal �߻� */
        if ( aStatement->spvEnv->createProc != NULL )
        {
            sBlock = aStatement->spvEnv->createProc->block;

            IDE_DASSERT( sBlock != NULL );

            if ( sBlock->isAutonomousTransBlock == ID_TRUE )
            {
                sqlInfo.setSourceInfo(
                    aStatement,
                    &sProcStmtFetch->cursorNode->position );
                IDE_RAISE(ERR_CANNOT_USE_IN_AUTONOMOUSTRANS_BLOCK);
            }
        }
        else
        {
            // Nothing to do.
        }

        IDE_TEST( qsvRefCursor::validateFetch(
                      aStatement,
                      aProcStmts,
                      aParentStmt,
                      aPurpose )
                  != IDE_SUCCESS );
    }
    else
    {
        sProcStmtFetch->isRefCursor = ID_FALSE;

        IDE_TEST( qsvProcVar::searchCursor(
                      aStatement,
                      sProcStmtFetch->cursorNode,
                      &sIsFound,
                      &sCursorDef )
                  != IDE_SUCCESS );
        
        if( sIsFound == ID_TRUE )
        {
            IDE_TEST( qsvCursor::validateFetch( aStatement,
                                                aProcStmts,
                                                aParentStmt,
                                                aPurpose )
                      != IDE_SUCCESS );
        }
        else
        {
            sqlInfo.setSourceInfo(
                aStatement,
                &sProcStmtFetch->cursorNode->position );
            IDE_RAISE(ERR_NOT_FOUND_VAR);
        }
    }
    
    return IDE_SUCCESS;

  
    IDE_EXCEPTION(ERR_NOT_FOUND_VAR);
    {
        (void)sqlInfo.init(aStatement->qmeMem);
        IDE_SET(
            ideSetErrorCode(qpERR_ABORT_QSV_NOT_EXIST_VARIABLE_NAME_SQLTEXT,
                            sqlInfo.getErrMessage() ));
        (void)sqlInfo.fini();
    }
    IDE_EXCEPTION( ERR_CANNOT_USE_IN_AUTONOMOUSTRANS_BLOCK )
    {
        (void)sqlInfo.init(aStatement->qmeMem);
        IDE_SET(
            ideSetErrorCode( qpERR_ABORT_QSV_CANNOT_USE_IN_AUTONOMOUS_TRANSACTION,
                             sqlInfo.getErrMessage() ));
        (void)sqlInfo.fini();
    }
    IDE_EXCEPTION_END;

    return IDE_FAILURE;
}

IDE_RC qsvProcStmts::validateClose( qcStatement     * aStatement,
                                    qsProcStmts     * aProcStmts,
                                    qsProcStmts     * aParentStmt,
                                    qsValidatePurpose aPurpose )
{
/***********************************************************************
 *
 *  Description : PROJ-1386 Dynamic-SQL
 *
 *
 *  Implementation :
 *         (1) ref cursor variable�� ���� ������, cursor�� ���� ������ �з�
 *         (2) �з��� ����� qsProcStmtFetch����ü�� ����
 *         (3) ������ �´� validate�Լ��� ȣ��
 *
 ***********************************************************************/

    idBool             sIsFound;
    qcuSqlSourceInfo   sqlInfo;
    qsProcStmtClose  * sProcStmtClose = (qsProcStmtClose*)aProcStmts;
    qsCursors        * sCursorDef;
    qsProcStmtBlock  * sBlock;
    qsVariables      * sVariables;
    
    QS_SET_PARENT_STMT( aProcStmts, aParentStmt );
    
    aStatement->spvEnv->currStmt = aProcStmts;

    IDE_TEST( qsvRefCursor::searchRefCursorVar(
                  aStatement,
                  sProcStmtClose->cursorNode,
                  &sVariables,
                  &sIsFound )
              != IDE_SUCCESS );
    
    if( sIsFound == ID_TRUE )
    {
        sProcStmtClose->isRefCursor = ID_TRUE;

        /* BUG-38509 autonomous transaction
           autonomous transaction block���� ref cursor�� ������� �� �Ѵ�.
           ��� �� execute�� �� fatal �߻� */
        if ( aStatement->spvEnv->createProc != NULL )
        {
            sBlock = aStatement->spvEnv->createProc->block;

            IDE_DASSERT( sBlock != NULL );

            if ( sBlock->isAutonomousTransBlock == ID_TRUE )
            {
                sqlInfo.setSourceInfo(
                    aStatement,
                    &sProcStmtClose->cursorNode->position );
                IDE_RAISE(ERR_CANNOT_USE_IN_AUTONOMOUSTRANS_BLOCK);
            }
        }
        else
        {
            // Nothing to do.
        }
    }
    else
    {
        sProcStmtClose->isRefCursor = ID_FALSE;

        IDE_TEST( qsvProcVar::searchCursor(
                      aStatement,
                      sProcStmtClose->cursorNode,
                      &sIsFound,
                      &sCursorDef )
                  != IDE_SUCCESS );

        if( sIsFound == ID_TRUE )
        {
            IDE_TEST( qsvCursor::validateClose( aStatement,
                                                aProcStmts,
                                                aParentStmt,
                                                aPurpose )
                      != IDE_SUCCESS );
        }
        else
        {
            sqlInfo.setSourceInfo(
                aStatement,
                &sProcStmtClose->cursorNode->position );
            IDE_RAISE(ERR_NOT_FOUND_VAR);
        }
    }
    
    return IDE_SUCCESS;

  
    IDE_EXCEPTION(ERR_NOT_FOUND_VAR);
    {
        (void)sqlInfo.init(aStatement->qmeMem);
        IDE_SET(
            ideSetErrorCode(qpERR_ABORT_QSV_NOT_EXIST_VARIABLE_NAME_SQLTEXT,
                            sqlInfo.getErrMessage() ));
        (void)sqlInfo.fini();
    }
    IDE_EXCEPTION( ERR_CANNOT_USE_IN_AUTONOMOUSTRANS_BLOCK )
    {
        (void)sqlInfo.init(aStatement->qmeMem);
        IDE_SET(
            ideSetErrorCode( qpERR_ABORT_QSV_CANNOT_USE_IN_AUTONOMOUS_TRANSACTION,
                             sqlInfo.getErrMessage() ));
        (void)sqlInfo.fini();
    }
    IDE_EXCEPTION_END;

    return IDE_FAILURE;
}

IDE_RC qsvProcStmts::validateNull( qcStatement * aStatement,
                                   qsProcStmts * aProcStmts,
                                   qsProcStmts * aParentStmt,
                                   qsValidatePurpose /* aPurpose */ )
{

    QS_SET_PARENT_STMT( aProcStmts, aParentStmt );

    aStatement->spvEnv->currStmt = aProcStmts;

    /* nothing to do */

    return IDE_SUCCESS;
}

IDE_RC qsvProcStmts::parseAssign(
    qcStatement * aStatement,
    qsProcStmts * aProcStmts)
{
    qsProcStmtAssign    * sASSIGN = (qsProcStmtAssign *)aProcStmts;

    // right node
    IDE_TEST(qmv::parseViewInExpression( aStatement, sASSIGN->rightNode )
             != IDE_SUCCESS);
    
    return IDE_SUCCESS;

  
    IDE_EXCEPTION_END;

    return IDE_FAILURE;
}

IDE_RC qsvProcStmts::validateAssign( qcStatement     * aStatement,
                                     qsProcStmts     * aProcStmts,
                                     qsProcStmts     * aParentStmt,
                                     qsValidatePurpose /* aPurpose */ )
{
    qsProcStmtAssign    * sASSIGN = (qsProcStmtAssign *)aProcStmts;
    qcuSqlSourceInfo      sqlInfo;
    idBool                sFind;
    qsVariables         * sLeftArrayVar;
    UInt                  sErrCode;         // PROJ-1073 Package
    qcNamePosition      * sNamePosition;

    QS_SET_PARENT_STMT( aProcStmts, aParentStmt );

    aStatement->spvEnv->currStmt = aProcStmts;

    sFind = ID_FALSE;

    // Validate left node.
    sNamePosition = &sASSIGN->leftNode->position;

    IDU_FIT_POINT_FATAL( "qsvProcStmts::validateAssign::__FT__::STAGE1" );

    // lvalue�� psm variable�� �����ϴ��� search.
    IDE_TEST(qsvProcVar::searchVarAndPara(
                 aStatement,
                 sASSIGN->leftNode,
                 ID_TRUE, // for OUTPUT
                 &sFind,
                 &sLeftArrayVar)
             != IDE_SUCCESS);

    //PROJ-1073 Package
    if( sFind == ID_FALSE )
    {
        if( qsvProcVar::searchVariableFromPkg(
                      aStatement,
                      sASSIGN->leftNode,
                      &sFind,
                      &sLeftArrayVar)
            != IDE_SUCCESS )
        {
            sErrCode = ideGetErrorCode();

            if( ( sErrCode == qpERR_ABORT_QCM_NOT_EXIST_USER ) ||
                ( sErrCode == qpERR_ABORT_QSV_INVALID_IDENTIFIER ) )
            {
                IDE_CLEAR();
            }
            else
            {
                // Nothing to do.
            }
        }
    }
    else
    {
        // Nothing to do.
    }

    if( sFind == ID_FALSE )
    {
        sqlInfo.setSourceInfo(
            aStatement,
            &sASSIGN->leftNode->position );
        IDE_RAISE(ERR_NOT_FOUND_VAR);
    }

    // lvalue�� psm������ �����ϹǷ� lvalue flag�� ����.
    // qtcColumn ��⿡�� estimate�� ������.
    sASSIGN->leftNode->lflag |= QTC_NODE_LVALUE_ENABLE;

    // PROJ-1904 Extend UDT
    if ( sLeftArrayVar->nocopyType == QS_NOCOPY )
    {
        sASSIGN->copyRef = ID_TRUE;
    }
    else
    {
        sASSIGN->copyRef = ID_FALSE;
    }

    // lvalue�� estimate�� �ϴ� ������
    // associative array�� index��� ����.
    IDE_TEST( qtc::estimate( sASSIGN->leftNode,
                             QC_SHARED_TMPLATE( aStatement ),
                             aStatement,
                             NULL,
                             NULL,
                             NULL )
              != IDE_SUCCESS);

    // BUG-42790 lvalue���� �׻� columnModule�̿����Ѵ�.
    IDE_ERROR_RAISE( ( sASSIGN->leftNode->node.module ==
                       &qtc::columnModule ),
                     ERR_UNEXPECTED_MODULE_ERROR );

    IDE_TEST_RAISE( qsv::checkNoSubquery(
                        aStatement,
                        sASSIGN->leftNode,
                        & sqlInfo ) != IDE_SUCCESS,
                    ERR_SUBQ_NOT_ALLOWED );

    if ( ( sASSIGN->leftNode->lflag & QTC_NODE_OUTBINDING_MASK )
         == QTC_NODE_OUTBINDING_DISABLE )
    {
        sqlInfo.setSourceInfo(
            aStatement,
            &sASSIGN->leftNode->position );
        IDE_RAISE(ERR_INOUT_TYPE_MISMATCH);
    }

    // Validate right node.
    sNamePosition = &sASSIGN->rightNode->position;

    // PROJ-1904 Extend UDT
    // ARR_TYPE1�� 1���� ARRAY, ARR_TYPE2�� 2���� ARRAY�� ��,
    // ���� ������ ���ؼ� rightNode�� QTC_NODE_LVALUE_ENABLE�� �����Ѵ�.
    // ���� flag�� �������� ������, no data found ������ �߻��Ѵ�.
    //
    // V1 NOCOPY ARR_TYPE1;
    // V2        ARR_TYPE2;
    // ..
    // V1 := V2[1];
    if ( (sLeftArrayVar->typeInfo != NULL) &&
         (sASSIGN->copyRef == ID_TRUE ) &&
         (sASSIGN->leftNode->node.arguments  == NULL) &&
         (sASSIGN->rightNode->node.arguments != NULL) )

    {
        if ( (sLeftArrayVar->typeInfo->flag & QTC_UD_TYPE_HAS_ARRAY_MASK) ==
             QTC_UD_TYPE_HAS_ARRAY_TRUE )
        {
            sASSIGN->rightNode->lflag |= QTC_NODE_LVALUE_ENABLE;
        }
        else
        {
            // Nothing to do.
        }
    }
    else
    {
        // Nothing to do.
    }

    IDE_TEST( qtc::estimate( sASSIGN->rightNode,
                             QC_SHARED_TMPLATE( aStatement ),
                             aStatement,
                             NULL,
                             NULL,
                             NULL )
              != IDE_SUCCESS);

    IDE_TEST_RAISE( qsv::checkNoSubquery(
                        aStatement,
                        sASSIGN->rightNode,
                        & sqlInfo ) != IDE_SUCCESS,
                    ERR_SUBQ_NOT_ALLOWED );

    return IDE_SUCCESS;

  

    IDE_EXCEPTION(ERR_NOT_FOUND_VAR);
    {
        (void)sqlInfo.init(aStatement->qmeMem);
        IDE_SET(
            ideSetErrorCode(qpERR_ABORT_QSV_NOT_EXIST_VARIABLE_NAME_SQLTEXT,
                            sqlInfo.getErrMessage() ));
        (void)sqlInfo.fini();
    }
    IDE_EXCEPTION(ERR_SUBQ_NOT_ALLOWED);
    {
        (void)sqlInfo.init(aStatement->qmeMem);
        IDE_SET(
            ideSetErrorCode( qpERR_ABORT_QSV_NOT_ALLOWED_SUBQUERY_SQLTEXT,
                             sqlInfo.getErrMessage() ));
        (void)sqlInfo.fini();
    }
    IDE_EXCEPTION(ERR_INOUT_TYPE_MISMATCH);
    {
        (void)sqlInfo.init(aStatement->qmeMem);
        IDE_SET(
            ideSetErrorCode(
                qpERR_ABORT_QSV_PROC_ASSIGN_LVALUE_NO_READONLY_VAR_SQLTEXT,
                sqlInfo.getErrMessage() ));
        (void)sqlInfo.fini();
    }
    IDE_EXCEPTION( ERR_UNEXPECTED_MODULE_ERROR )
    {
        IDE_SET( ideSetErrorCode( qpERR_ABORT_QMC_UNEXPECTED_ERROR,
                                  "qsvProcStmts::validateAssign",
                                  "The module is unexpected" ) );
    }
    IDE_EXCEPTION_END;

    // BUG-38883 print error position
    if ( ideHasErrorPosition() == ID_FALSE )
    {
        sqlInfo.setSourceInfo( aStatement,
                               sNamePosition );

        // set original error code.
        qsxEnv::setErrorCode( QC_QSX_ENV(aStatement) );

        (void)sqlInfo.initWithBeforeMessage(
            QC_QME_MEM(aStatement) );

        IDE_SET(
            ideSetErrorCode(qpERR_ABORT_QSX_SQLTEXT_WRAPPER,
                            sqlInfo.getBeforeErrMessage(),
                            sqlInfo.getErrMessage()));
        (void)sqlInfo.fini();

        // set sophisticated error message.
        qsxEnv::setErrorMessage( QC_QSX_ENV(aStatement) );
    }
    else
    {
        // Nothing to do.
    }

    return IDE_FAILURE;
}

IDE_RC qsvProcStmts::validateRaise( qcStatement     * aStatement,
                                    qsProcStmts     * aProcStmts,
                                    qsProcStmts     * aParentStmt,
                                    qsValidatePurpose /* aPurpose */ )
{
    qsProcStmtRaise  * sRAISE = (qsProcStmtRaise *)aProcStmts;
    qcuSqlSourceInfo   sqlInfo;

    QS_SET_PARENT_STMT( aProcStmts, aParentStmt );

    aStatement->spvEnv->currStmt = aProcStmts;

    if( sRAISE->exception == NULL )
    {
        // PROJ-1335, To fix BUG-13207  RAISE ����
        // exception�� null�̶�� exception handling ���Ͽ���
        // ����� �����.
        // exceptionCount�� ����� ��� exception handler ���ο� ������
        // �� �� ����.
        if (aStatement->spvEnv->exceptionCount <= 0)
        {
            sqlInfo.setSourceInfo( aStatement,
                                   &aProcStmts->pos );
            IDE_RAISE(ERR_NOT_USE_IN_EXCEPTION_HANDLER);
        }
        else
        {
            // Nothing to do.
        }
    }
    else
    {
        IDE_TEST(getException(aStatement, sRAISE->exception) != IDE_SUCCESS);
    }

    return IDE_SUCCESS;

  
    IDE_EXCEPTION(ERR_NOT_USE_IN_EXCEPTION_HANDLER);
    {
        (void)sqlInfo.init(aStatement->qmeMem);
        IDE_SET(
            ideSetErrorCode(qpERR_ABORT_QSV_PROC_NOT_USE_IN_EXCEPTION_HANDLER,
                            sqlInfo.getErrMessage() )
            );
        (void)sqlInfo.fini();
    }
    IDE_EXCEPTION_END;

    return IDE_FAILURE;
}

IDE_RC qsvProcStmts::parseReturn(
    qcStatement * aStatement,
    qsProcStmts * aProcStmts)
{
    qsProcStmtReturn    * sRETURN = (qsProcStmtReturn *)aProcStmts;

    if (sRETURN->returnNode != NULL)
    {
        IDE_TEST(qmv::parseViewInExpression( aStatement, sRETURN->returnNode )
                 != IDE_SUCCESS);
    }

    return IDE_SUCCESS;

  
    IDE_EXCEPTION_END;

    return IDE_FAILURE;
}

IDE_RC qsvProcStmts::validateReturn( qcStatement     * aStatement,
                                     qsProcStmts     * aProcStmts,
                                     qsProcStmts     * aParentStmt,
                                     qsValidatePurpose /* aPurpose */ )
{
    qsProcParseTree     * sParseTree;
    qsProcStmtReturn    * sRETURN = (qsProcStmtReturn *)aProcStmts;
    qcuSqlSourceInfo      sqlInfo;

    QS_SET_PARENT_STMT( aProcStmts, aParentStmt );

    aStatement->spvEnv->currStmt = aProcStmts;

    sParseTree = aStatement->spvEnv->createProc;

    // PROJ-1073 Package
    /* BUG-40013
       package�� initialize section�� procedure body(begin ~ end)�� �����ϰ� �����ϸ�,
       �ش� �κп� ���� validation ���������� qsProcParseTree�� NULL�̴�. */
    if ( sParseTree == NULL )
    {
        if ( ( aStatement->spvEnv->createPkg != NULL ) &&
             ( sRETURN->returnNode != NULL ) )
        {
            sqlInfo.setSourceInfo( aStatement,
                                   &sRETURN->common.pos );
            IDE_RAISE(ERR_HAVE_RETURN_VALUE);
        }
        else
        {
            // Nothing to do.
        }
    }
    else
    {
        // check PROCEDURE or FUNCTION
        if (sParseTree->returnTypeVar == NULL) // procedure
        {
            if (sRETURN->returnNode != NULL)
            {
                sqlInfo.setSourceInfo( aStatement,
                                       &sRETURN->common.pos );
                IDE_RAISE(ERR_HAVE_RETURN_VALUE);
            }
            else
            {
                /* Do Nothing */
            }
        }
        else                                    // function
        {
            if (sRETURN->returnNode == NULL)
            {
                sqlInfo.setSourceInfo( aStatement,
                                       &sRETURN->common.pos );
                IDE_RAISE(ERR_NOT_HAVE_RETURN_VALUE);
            }
            else
            {
                // validate RETURN expression
                IDE_TEST(qtc::estimate(
                        sRETURN->returnNode,
                        QC_SHARED_TMPLATE(aStatement),
                        aStatement,
                        NULL,
                        NULL,
                        NULL )
                    != IDE_SUCCESS);

                IDE_TEST_RAISE( qsv::checkNoSubquery(
                                    aStatement,
                                    sRETURN->returnNode,
                                    & sqlInfo ) != IDE_SUCCESS,
                                ERR_SUBQ_NOT_ALLOWED );
            }
        }
    }

    // set RETURN STMT MASK
    aStatement->spvEnv->flag &= ~QSV_ENV_RETURN_STMT_MASK;
    aStatement->spvEnv->flag |= QSV_ENV_RETURN_STMT_EXIST;

    return IDE_SUCCESS;

  
    IDE_EXCEPTION(ERR_SUBQ_NOT_ALLOWED);
    {
        (void)sqlInfo.init(aStatement->qmeMem);
        IDE_SET(
            ideSetErrorCode( qpERR_ABORT_QSV_NOT_ALLOWED_SUBQUERY_SQLTEXT,
                             sqlInfo.getErrMessage() ));
        (void)sqlInfo.fini();
    }
    IDE_EXCEPTION(ERR_HAVE_RETURN_VALUE);
    {
        (void)sqlInfo.init(aStatement->qmeMem);
        IDE_SET(
            ideSetErrorCode(qpERR_ABORT_QSV_PROC_HAVE_RETURN_VALUE_SQLTEXT,
                            sqlInfo.getErrMessage() ));
        (void)sqlInfo.fini();
    }
    IDE_EXCEPTION(ERR_NOT_HAVE_RETURN_VALUE);
    {
        (void)sqlInfo.init(aStatement->qmeMem);
        IDE_SET(
            ideSetErrorCode(qpERR_ABORT_QSV_PROC_NOT_HAVE_RETURN_VALUE_SQLTEXT,
                            sqlInfo.getErrMessage() ));
        (void)sqlInfo.fini();
    }
    IDE_EXCEPTION_END;

    return IDE_FAILURE;
}

IDE_RC qsvProcStmts::validateLabel( qcStatement     * aStatement,
                                    qsProcStmts     * aProcStmts,
                                    qsProcStmts     * aParentStmt,
                                    qsValidatePurpose /* aPurpose */ )
{
    qsProcStmtLabel     * sLABEL = (qsProcStmtLabel *)aProcStmts;
    qsProcStmts         * sNextStmt;
    qsLabels            * sCurrLabel;
    qcuSqlSourceInfo      sqlInfo;

    QS_SET_PARENT_STMT( aProcStmts, aParentStmt );

    aStatement->spvEnv->currStmt = aProcStmts;

    if (sLABEL->common.next == NULL)
    {
        sqlInfo.setSourceInfo(
            aStatement,
            &sLABEL->common.pos );
        IDE_RAISE(ERR_NOT_EXIST_STMT);
    }
    else
    {
        // Nothing to do.
    }

    // BUG-27442
    // Validate length of Label name
    if ( sLABEL->labelNamePos.size > QC_MAX_OBJECT_NAME_LEN )
    {
        sqlInfo.setSourceInfo(
            aStatement,
            &sLABEL->labelNamePos );
        IDE_RAISE( ERR_MAX_NAME_LEN_OVERFLOW );
    }
    else
    {
        // Nothing to do.
    }

    // identification
    sLABEL->id = qsvEnv::getNextId(aStatement->spvEnv);

    IDE_TEST(STRUCT_ALLOC(QC_QMP_MEM(aStatement), qsLabels, &sCurrLabel)
             != IDE_SUCCESS);

    sCurrLabel->namePos = sLABEL->labelNamePos;
    sCurrLabel->id      = sLABEL->id;
    sCurrLabel->stmt    = NULL;
    sCurrLabel->next    = NULL;

    // for checking local variables or cursors name scope
    for (sNextStmt = aProcStmts->next;
         sNextStmt != NULL;
         sNextStmt = sNextStmt->next)
    {
        if (sNextStmt->validate == qsvProcStmts::validateBlock)
        {
            sCurrLabel->stmt = sNextStmt;
            sCurrLabel->next = sNextStmt->parentLabels;
            sNextStmt->parentLabels = sCurrLabel;
            break;
        }
        else if (sNextStmt->validate == qsvProcStmts::validateWhile)
        {
            sCurrLabel->stmt = sNextStmt;
            sCurrLabel->next = sNextStmt->parentLabels;
            sNextStmt->parentLabels = sCurrLabel;
            break;
        }
        else if (sNextStmt->validate == qsvProcStmts::validateFor)
        {
            sCurrLabel->stmt = sNextStmt;
            sCurrLabel->next = sNextStmt->parentLabels;
            sNextStmt->parentLabels = sCurrLabel;
            break;
        }
        else if (sNextStmt->validate == qsvCursor::validateCursorFor)
        {
            sCurrLabel->stmt = sNextStmt;
            sCurrLabel->next = sNextStmt->parentLabels;
            sNextStmt->parentLabels = sCurrLabel;
            break;
        }
        // To Fix BUG-13144
        // LABEL�� BLOCK, LOOP�ٷ� ���� ���ų� �̷� LABEL�ٷ� ���� �־��
        // �Ѵ�.
        // ex)
        // <<LABEL1>> -- �̿� ���� ��쵵 �����.
        // <<LABEL2>>
        // FOR I IN 1 .. 10 LOOP
        // ...
        else if (sNextStmt->validate == qsvProcStmts::validateLabel)
        {
            continue;
        }
        else
        {
            sCurrLabel->stmt = sNextStmt;
            break;
        }
    }

    // PROJ-1335, To fix BUG-12475
    // label�� parent statement�� �����Ͽ� �޾Ƴ��´�.
    // goto ���� �����ϱ� ����
    IDE_TEST( setLabelToParentStmt( aStatement, aParentStmt, sCurrLabel )
              != IDE_SUCCESS );

    return IDE_SUCCESS;

  
    IDE_EXCEPTION(ERR_NOT_EXIST_STMT);
    {
        (void)sqlInfo.init(aStatement->qmeMem);
        IDE_SET(
            ideSetErrorCode(qpERR_ABORT_QSV_PROC_NOT_EXIST_STMT_SQLTEXT,
                            sqlInfo.getErrMessage() ));
        (void)sqlInfo.fini();
    }
    IDE_EXCEPTION(ERR_MAX_NAME_LEN_OVERFLOW)
    {
        (void)sqlInfo.init(aStatement->qmeMem);
        IDE_SET(
            ideSetErrorCode(qpERR_ABORT_QCP_MAX_NAME_LENGTH_OVERFLOW,
                            sqlInfo.getErrMessage() ));
        (void)sqlInfo.fini();
    }
    IDE_EXCEPTION_END;

    return IDE_FAILURE;
}

IDE_RC qsvProcStmts::parseThen( qcStatement     * aStatement,
                                qsProcStmts     * aProcStmts )
{
    qsProcStmtThen * sTHEN = (qsProcStmtThen*)aProcStmts;
    qsProcStmts    * sProcStmt;

    for (sProcStmt = sTHEN->thenStmts;
         sProcStmt != NULL;
         sProcStmt = sProcStmt->next)
    {
        IDE_TEST(sProcStmt->parse(aStatement, sProcStmt) != IDE_SUCCESS);
    }

    return IDE_SUCCESS;

  
    IDE_EXCEPTION_END;

    return IDE_FAILURE;
}

IDE_RC qsvProcStmts::validateThen( qcStatement     * aStatement,
                                   qsProcStmts     * aProcStmts,
                                   qsProcStmts     * aParentStmt,
                                   qsValidatePurpose aPurpose  )
{
    qsProcStmtThen * sTHEN = (qsProcStmtThen*)aProcStmts;
    qsProcStmts    * sProcStmt;

    QS_SET_PARENT_STMT( aProcStmts, aParentStmt );

    aStatement->spvEnv->currStmt = aProcStmts;

    for (sProcStmt = sTHEN->thenStmts;
         sProcStmt != NULL;
         sProcStmt = sProcStmt->next)
    {
        IDE_TEST( sProcStmt->validate(aStatement, sProcStmt, aProcStmts, aPurpose)
                  != IDE_SUCCESS);
    }

    return IDE_SUCCESS;

  
    IDE_EXCEPTION_END;

    return IDE_FAILURE;
}

IDE_RC qsvProcStmts::parseElse( qcStatement     * aStatement,
                                qsProcStmts     * aProcStmts )
{
    qsProcStmtElse * sELSE = (qsProcStmtElse*)aProcStmts;
    qsProcStmts    * sProcStmt;

    for (sProcStmt = sELSE->elseStmts;
         sProcStmt != NULL;
         sProcStmt = sProcStmt->next)
    {
        IDE_TEST(sProcStmt->parse(aStatement, sProcStmt) != IDE_SUCCESS);
    }

    return IDE_SUCCESS;

  
    IDE_EXCEPTION_END;

    return IDE_FAILURE;
}

IDE_RC qsvProcStmts::validateElse( qcStatement     * aStatement,
                                   qsProcStmts     * aProcStmts,
                                   qsProcStmts     * aParentStmt,
                                   qsValidatePurpose aPurpose  )
{
    qsProcStmtElse * sELSE = (qsProcStmtElse*)aProcStmts;
    qsProcStmts    * sProcStmt;

    QS_SET_PARENT_STMT( aProcStmts, aParentStmt );

    aStatement->spvEnv->currStmt = aProcStmts;

    for (sProcStmt = sELSE->elseStmts;
         sProcStmt != NULL;
         sProcStmt = sProcStmt->next)
    {
        IDE_TEST( sProcStmt->validate(aStatement, sProcStmt, aProcStmts, aPurpose)
                  != IDE_SUCCESS);
    }

    return IDE_SUCCESS;

  
    IDE_EXCEPTION_END;

    return IDE_FAILURE;
}

IDE_RC qsvProcStmts::makeRelatedObjects(
    qcStatement     * aStatement,
    qcNamePosition  * aUserName,
    qcNamePosition  * aObjectName,
    qcmSynonymInfo  * aSynonymInfo,
    UInt              aTableID,
    SInt              aObjectType)
{
    qsRelatedObjects   * sCurrObject;
    qsRelatedObjects   * sObject;

    if (aStatement->spvEnv->relatedObjects == NULL)
    {
        // make new related object
        IDE_TEST( makeNewRelatedObject( aStatement,
                                        aUserName,
                                        aObjectName,
                                        aSynonymInfo,
                                        aTableID,
                                        aObjectType,
                                        &sCurrObject)
                  != IDE_SUCCESS);
        
        // connect
        sCurrObject->next = NULL;
        aStatement->spvEnv->relatedObjects = sCurrObject;
    }
    else
    {
        // make new related object
        IDE_TEST( makeNewRelatedObject( aStatement,
                                        aUserName,
                                        aObjectName,
                                        aSynonymInfo,
                                        aTableID,
                                        aObjectType,
                                        &sCurrObject)
                  != IDE_SUCCESS);
        
        // search same object
        for (sObject = aStatement->spvEnv->relatedObjects;
             sObject != NULL;
             sObject = sObject->next)
        {
            if (sObject->objectType == sCurrObject->objectType &&
                idlOS::strMatch(sObject->userName.name,
                                sObject->userName.size,
                                sCurrObject->userName.name,
                                sCurrObject->userName.size) == 0 &&
                idlOS::strMatch(sObject->objectName.name,
                                sObject->objectName.size,
                                sCurrObject->objectName.name,
                                sCurrObject->objectName.size) == 0)
            {
                // found
                break;
            }
        }
        
        // connect
        if (sObject == NULL)
        {
            sCurrObject->next = aStatement->spvEnv->relatedObjects;
            aStatement->spvEnv->relatedObjects = sCurrObject;
        }
    }

    return IDE_SUCCESS;

  
    IDE_EXCEPTION_END;

    return IDE_FAILURE;
}

IDE_RC qsvProcStmts::makeNewRelatedObject(
    qcStatement       * aStatement,
    qcNamePosition    * aUserName,
    qcNamePosition    * aObjectName,
    qcmSynonymInfo    * aSynonymInfo,
    UInt                aTableID,
    SInt                aObjectType,
    qsRelatedObjects ** aObject)
{
    qsRelatedObjects * sCurrObject;
    UInt               sConnectUserID;
    idBool             sExist         = ID_FALSE;
    iduVarMemList    * sMemory;

    sConnectUserID = QCG_GET_SESSION_USER_ID(aStatement);
    sMemory        = QC_QMP_MEM( aStatement );
    
    IDE_ASSERT( aSynonymInfo != NULL );
    
    IDE_TEST( STRUCT_ALLOC( sMemory,
                            qsRelatedObjects,
                            &sCurrObject ) != IDE_SUCCESS );

    // object type
    sCurrObject->userID     = 0;
    sCurrObject->objectID    = 0;
    sCurrObject->objectType = aObjectType;
    sCurrObject->tableID    = aTableID;

    if( sCurrObject->objectType == QS_TABLE )
    {
        // Nothing To Do
    }
    else if( sCurrObject->objectType == QS_LIBRARY )
    {
        // Nothing to do.
    }
    else /* QS_PROC or QS_FUNC */
    {
        IDE_TEST( qcmSynonym::resolvePSM( aStatement,
                                          *aUserName,
                                          *aObjectName,
                                          &( sCurrObject->objectID ),
                                          &( sCurrObject->userID ),
                                          & sExist,
                                          aSynonymInfo )
                  != IDE_SUCCESS);

        if (sExist == ID_FALSE)
        {
            // self procedure (recursive call in create procedure)
            sCurrObject->objectID = 0;
        }
        else
        {
            // Nothing To Do
        }
    }

    if( aSynonymInfo->isSynonymName == ID_TRUE )
    {
        //-------------------------------------
        // SYNONYM USER NAME
        //-------------------------------------
        sCurrObject->userName.size
            = idlOS::strlen( aSynonymInfo->objectOwnerName );

        IDE_TEST( sMemory->alloc( sCurrObject->userName.size + 1,
                                  (void**)&(sCurrObject->userName.name) )
                  != IDE_SUCCESS );

        idlOS::memcpy( sCurrObject->userName.name,
                       &(aSynonymInfo->objectOwnerName),
                       sCurrObject->userName.size );

        sCurrObject->userName.name[sCurrObject->userName.size] = '\0';

        IDE_TEST( qcmUser::getUserID( aStatement,
                                      aSynonymInfo->objectOwnerName,
                                      (UInt)(sCurrObject->userName.size),
                                      &(sCurrObject->userID) )
                  != IDE_SUCCESS);

        //-------------------------------------
        // SYNONYM OBJECT NAME
        //-------------------------------------
        sCurrObject->objectName.size
            = idlOS::strlen( aSynonymInfo->objectName );

        IDE_TEST( sMemory->alloc( sCurrObject->objectName.size + 1,
                                  (void**)&(sCurrObject->objectName.name) )
                  != IDE_SUCCESS );

        idlOS::memcpy( sCurrObject->objectName.name,
                       &(aSynonymInfo->objectName),
                       sCurrObject->objectName.size );

        sCurrObject->objectName.name[sCurrObject->objectName.size] = '\0';
    }
    else
    {
        IDE_TEST( STRUCT_ALLOC_WITH_SIZE( sMemory,
                                          SChar,
                                          QC_MAX_OBJECT_NAME_LEN + 1,
                                          &(sCurrObject->userName.name))
                  != IDE_SUCCESS);

        //-------------------------------------
        // USER NAME
        //-------------------------------------
        sCurrObject->userName.size = aUserName->size;
        if (sCurrObject->userName.size > 0)
        {
            QC_STR_COPY( sCurrObject->userName.name, *aUserName );

            IDE_TEST(qcmUser::getUserID( aStatement, *aUserName,
                                         &(sCurrObject->userID) )
                     != IDE_SUCCESS);
        }
        else
        {
            // connect user name
            IDE_TEST(qcmUser::getUserName(
                         aStatement,
                         sConnectUserID,
                         sCurrObject->userName.name)
                     != IDE_SUCCESS);

            sCurrObject->userName.size
                = idlOS::strlen(sCurrObject->userName.name);
            sCurrObject->userID = sConnectUserID;
        }

        //-------------------------------------
        // OBJECT NAME
        //-------------------------------------
        sCurrObject->objectName.size = aObjectName->size;
        IDE_TEST( STRUCT_ALLOC_WITH_SIZE( sMemory,
                                          SChar,
                                          (sCurrObject->objectName.size+1),
                                          &(sCurrObject->objectName.name ) ) 
                  != IDE_SUCCESS);

        QC_STR_COPY( sCurrObject->objectName.name, *aObjectName );

    }

    // objectNamePos
    SET_POSITION( sCurrObject->objectNamePos, (*aObjectName) );

    // return
    *aObject = sCurrObject;

    return IDE_SUCCESS;

  
    IDE_EXCEPTION_END;

    return IDE_FAILURE;
}

IDE_RC qsvProcStmts::makeProcSynonymList( qcStatement    * aStatement,
                                          qcmSynonymInfo * aSynonymInfo,
                                          qcNamePosition   aUserName,
                                          qcNamePosition   aObjectName,
                                          qsOID            aProcID )
{
    iduVarMemList * sMemory;
    qsSynonymList * sSynonym;
    idBool          sExist = ID_FALSE;

    sMemory = QC_QMP_MEM( aStatement );
    
    if ( aSynonymInfo->isSynonymName == ID_TRUE )
    {
        IDE_DASSERT( QC_IS_NULL_NAME( aObjectName ) == ID_FALSE );

        for ( sSynonym = aStatement->spvEnv->objectSynonymList;
              sSynonym != NULL;
              sSynonym = sSynonym->next )
        {
            if ( QC_IS_NULL_NAME( aUserName ) == ID_TRUE )
            {
                if ( (sSynonym->userName[0] == '\0') &&
                     (idlOS::strMatch( sSynonym->objectName,
                                       idlOS::strlen( sSynonym->objectName ),
                                       aObjectName.stmtText + aObjectName.offset,
                                       aObjectName.size ) == 0) &&
                     (sSynonym->isPublicSynonym == aSynonymInfo->isPublicSynonym) )
                {
                    IDE_DASSERT( sSynonym->objectID == aProcID );
                    
                    sExist = ID_TRUE;
                    break;
                }
                else
                {
                    // Nothing to do.
                }
            }
            else
            {
                if ( (idlOS::strMatch( sSynonym->userName,
                                       idlOS::strlen( sSynonym->userName ),
                                       aUserName.stmtText + aUserName.offset,
                                       aUserName.size ) == 0) &&
                     (idlOS::strMatch( sSynonym->objectName,
                                       idlOS::strlen( sSynonym->objectName ),
                                       aObjectName.stmtText + aObjectName.offset,
                                       aObjectName.size ) == 0) &&
                     (sSynonym->isPublicSynonym == aSynonymInfo->isPublicSynonym) )
                {
                    IDE_DASSERT( sSynonym->objectID == aProcID );
                    
                    sExist = ID_TRUE;
                    break;
                }
                else
                {
                    // Nothing to do.
                }
            }
        }
        
        if( sExist == ID_FALSE )
        {
            IDE_TEST( sMemory->alloc( ID_SIZEOF(qsSynonymList),
                                      (void**)&sSynonym )
                      != IDE_SUCCESS );
            
            if ( QC_IS_NULL_NAME( aUserName ) == ID_TRUE )
            {
                sSynonym->userName[0] = '\0';
            }
            else
            {
                idlOS::strncpy( sSynonym->userName,
                                aUserName.stmtText + aUserName.offset,
                                aUserName.size );
                sSynonym->userName[aUserName.size] = '\0';
            }
            
            idlOS::strncpy( sSynonym->objectName,
                            aObjectName.stmtText + aObjectName.offset,
                            aObjectName.size );
            sSynonym->objectName[aObjectName.size] = '\0';
            
            sSynonym->isPublicSynonym = aSynonymInfo->isPublicSynonym;
            sSynonym->objectID = aProcID;
            
            sSynonym->next = aStatement->spvEnv->objectSynonymList;
            
            // �����Ѵ�.
            aStatement->spvEnv->objectSynonymList = sSynonym;
        }
        else
        {
            // Nothing to do.
        }
    }
    else
    {
        // Nothing to do.
    }
    
    return IDE_SUCCESS;

  
    IDE_EXCEPTION_END;

    return IDE_FAILURE;
}

IDE_RC
qsvProcStmts::validateExceptionHandler( qcStatement       * aStatement,
                                        qsProcStmtBlock   * aProcBLOCK,
                                        qsPkgStmtBlock    * aPkgBLOCK,
                                        qsProcStmts       * aParentStmt,
                                        qsValidatePurpose   aPurpose )
{
    qsExceptionHandlers * sExceptionHandler;
    qsExceptions        * sHandledExceptions;
    qsExceptions        * sException;
    qsExceptions        * sTmpException;
    qsProcStmts         * sProcStmt;
    qcuSqlSourceInfo      sqlInfo;
    // BUG-37501
    qsProcStmts         * sExceptionStmt;
    qsProcStmtException * sExceptionBlock;
    /* BUG-41240 EXCEPTION_INIT Pragma */
    SChar sTmpExceptionName[QC_MAX_OBJECT_NAME_LEN + 1];
    SChar sExceptionName[QC_MAX_OBJECT_NAME_LEN + 1];

    // initialize
    sHandledExceptions = NULL;

    /* aProcBLOCK�� null�� ���� package�� initialize section����
       exception ó�� �� �̴�. */
    if( aProcBLOCK != NULL )
    {
        IDE_DASSERT( aPkgBLOCK == NULL );
        sExceptionStmt = aProcBLOCK->exception;
    }
    else
    {
        IDE_DASSERT( aPkgBLOCK != NULL );
        sExceptionStmt = aPkgBLOCK->exception;
    }

    QS_SET_PARENT_STMT( sExceptionStmt, aParentStmt );

    sExceptionBlock = (qsProcStmtException *)sExceptionStmt;

    for (sExceptionHandler = sExceptionBlock->exceptionHandlers;
         sExceptionHandler != NULL;
         sExceptionHandler = sExceptionHandler->next)
    {
        // exception name
        for (sException = sExceptionHandler->exceptions;
             sException != NULL;
             sException = sException->next)
        {
            IDE_TEST(getException(aStatement, sException) != IDE_SUCCESS);

            // search
            for (sTmpException = sHandledExceptions;
                 sTmpException != NULL;
                 sTmpException = sTmpException->next)
            {
                // PROJ-1073 Package
                // package������ exception���� ���𰡴��ϸ�, �� id�� ������.
                // �׷��� ������, id������ ã����,
                // ������ id�� ���� �ٸ� package�� exception������ ���,
                // �������� ������ ������ excpetion������� �Ǵ��Ѵ�.
                // ����, �̸� �����ϱ� ���ؼ��� exception������ id��
                // exception������ ����� ��ü�� objectID�� ���� Ȯ���ؾ� �Ѵ�.
                if ( ( sTmpException->id == sException->id ) && 
                     ( sTmpException->objectID == sException->objectID ) )
                {
                    sqlInfo.setSourceInfo( aStatement,
                                           &sException->exceptionNamePos );
                    IDE_RAISE(ERR_DUP_EXCEPTION_IN_HANDLER);
                }

                /* BUG-41240 EXCEPTION_INIT Pragma */
                if ( checkDupErrorCode( sTmpException, sException ) == ID_TRUE )
                {
                    sqlInfo.setSourceInfo( aStatement,
                                           & sException->exceptionNamePos );

                    IDE_RAISE( ERR_REDUNDANT_EXCEPTIONS );
                }
                else
                {
                    // Nothing to do.
                }
            }

            // make handled exception list
            IDE_TEST(STRUCT_ALLOC(QC_QME_MEM(aStatement), qsExceptions, &sTmpException)
                     != IDE_SUCCESS);

            idlOS::memcpy(sTmpException, sException, ID_SIZEOF(qsExceptions));

            // connect
            sTmpException->next = sHandledExceptions;
            sHandledExceptions = sTmpException;
        }

        // PROJ-1335, To fix BUG-13207
        // validate for RAISE
        // RAISE;�� �ݵ�� exception handler���ο����� ���Ǿ�� �Ѵ�.
        (aStatement->spvEnv->exceptionCount)++;

        // exception handler action statement
        sProcStmt = sExceptionHandler->actionStmt;

        IDE_TEST(sProcStmt->parse(aStatement, sProcStmt) != IDE_SUCCESS);

        // BUG-37501
        // exceptionHandlers�� parent�� exception block�̴�.
        IDE_TEST(sProcStmt->validate(aStatement, sProcStmt, sExceptionStmt, aPurpose )
                 != IDE_SUCCESS);

        // BUG-37501 
        // exception block ��ü���� label�� Ȱ�밡���ϵ��� excpetion block�� childlabel�� �̾���� �Ѵ�.
        IDE_TEST( connectChildLabel( aStatement, sProcStmt, sExceptionStmt ) != IDE_SUCCESS );

        // PROJ-1335, To fix BUG-13207
        // validate for RAISE
        // RAISE;�� �ݵ�� exception handler���ο����� ���Ǿ�� �Ѵ�.
        (aStatement->spvEnv->exceptionCount)--;
    }

    return IDE_SUCCESS;

  
    IDE_EXCEPTION( ERR_REDUNDANT_EXCEPTIONS )
    {
        QC_STR_COPY( sTmpExceptionName,
                     sTmpException->exceptionNamePos );

        QC_STR_COPY( sExceptionName,
                     sException->exceptionNamePos );

        (void)sqlInfo.init(aStatement->qmeMem);
        IDE_SET(
            ideSetErrorCode(
                qpERR_ABORT_QSV_REDUNDANT_EXCEPTIONS_IN_HANDLER,
                sTmpExceptionName,
                sExceptionName,
                sqlInfo.getErrMessage() ));
        (void)sqlInfo.fini();
    }
    IDE_EXCEPTION(ERR_DUP_EXCEPTION_IN_HANDLER);
    {
        (void)sqlInfo.init(aStatement->qmeMem);
        IDE_SET(
            ideSetErrorCode(
                qpERR_ABORT_QSV_PROC_DUP_EXCEPTION_IN_HANDLER_SQLTEXT,
                sqlInfo.getErrMessage() ));
        (void)sqlInfo.fini();
    }
    IDE_EXCEPTION_END;

    return IDE_FAILURE;
}

IDE_RC qsvProcStmts::checkSelectIntoClause(
    qcStatement     * aStatement,
    qsProcStmts     * aProcStmt,
    qmsQuerySet     * aQuerySet)
{
    qcuSqlSourceInfo    sqlInfo;
    qsProcStmtSql     * sSql;
    qtcNode           * sCurrIntoVar;
    idBool              sFindVar;
    qsVariables       * sArrayVariable;
    mtcColumn         * sMtcColumn;
    // PROJ-1073 Package
    UShort              sTable  = 0;
    UShort              sColumn = 0;

    if (aQuerySet->setOp == QMS_NONE)
    {
        if (aQuerySet->SFWGH->intoVariables == NULL)
        {
            sqlInfo.setSourceInfo(
                aStatement,
                &aProcStmt->pos );
            IDE_RAISE(ERR_DO_NOT_HAVE_INTO);
        }
        else
        {
            sSql = (qsProcStmtSql*)aProcStmt;

            // 1.1 into���� ��� procedure������ ����.
            for( sCurrIntoVar = sSql->intoVariables->intoNodes;
                 sCurrIntoVar != NULL;
                 sCurrIntoVar = (qtcNode *)(sCurrIntoVar->node.next) )
            {
                // PROJ-1073 Package
                if( sCurrIntoVar->node.objectID != 0 )
                {
                    sTable = sCurrIntoVar->node.table;
                    sColumn = sCurrIntoVar->node.column;
                }
                else
                {
                    // Nothing to do.
                }

                IDE_TEST(qsvProcVar::searchVarAndPara(
                             aStatement,
                             sCurrIntoVar,
                             ID_TRUE, // for OUTPUT
                             &sFindVar,
                             &sArrayVariable)
                         != IDE_SUCCESS);

                // PROJ-1073 Package
                if( sFindVar == ID_FALSE )
                {
                    IDE_TEST( qsvProcVar::searchVariableFromPkg(
                            aStatement,
                            sCurrIntoVar,
                            &sFindVar,
                            &sArrayVariable )
                        != IDE_SUCCESS );
                } 
 
                // PROJ-1073 Package
                if( sCurrIntoVar->node.objectID != 0 )
                {
                    sCurrIntoVar->node.table = sTable;
                    sCurrIntoVar->node.column = sColumn;
                }
                else
                {
                    // Nothing to do.
                }

                // lvalue�� psm������ �����ϹǷ� lvalue flag�� ����.
                // qtcColumn ��⿡�� estimate�� ������.
                sCurrIntoVar->lflag |= QTC_NODE_LVALUE_ENABLE;
                
                IDE_TEST( qtc::estimate( sCurrIntoVar,
                                         QC_SHARED_TMPLATE(aStatement), 
                                         aStatement,
                                         NULL,
                                         NULL,
                                         NULL )
                          != IDE_SUCCESS );

                if ( ( sCurrIntoVar->lflag & QTC_NODE_OUTBINDING_MASK )
                     == QTC_NODE_OUTBINDING_DISABLE )
                {
                    sqlInfo.setSourceInfo(
                        aStatement,
                        &sCurrIntoVar->position );
                    IDE_RAISE(ERR_INOUT_TYPE_MISMATCH);
                }
                else
                {
                    // Nothing to do.
                }

                sMtcColumn = QTC_STMT_COLUMN(
                    aStatement,
                    sCurrIntoVar );

                if( ( sMtcColumn->module->id == MTD_ROWTYPE_ID ) ||
                    ( sMtcColumn->module->id == MTD_RECORDTYPE_ID ) )
                {
                    sSql->isIntoVarRecordType = ID_TRUE;
                }
                else
                {
                    // Nothing to do.
                }
            }
        }
    }
    else // UNION, UNION ALL, INTERSECT, MINUS
    {
        IDE_TEST(checkSelectIntoClause(
                     aStatement,
                     aProcStmt,
                     aQuerySet->left)
                 != IDE_SUCCESS);
    }

    return IDE_SUCCESS;

  
    IDE_EXCEPTION(ERR_DO_NOT_HAVE_INTO);
    {
        (void)sqlInfo.init(aStatement->qmeMem);
        IDE_SET(
            ideSetErrorCode(qpERR_ABORT_QSV_NOT_HAVE_INTO_SQLTEXT,
                            sqlInfo.getErrMessage() ));
        (void)sqlInfo.fini();
    }
    IDE_EXCEPTION(ERR_INOUT_TYPE_MISMATCH);
    {
        (void)sqlInfo.init(aStatement->qmeMem);
        IDE_SET(
            ideSetErrorCode
            (   qpERR_ABORT_QSV_PROC_SELECT_INTO_NO_READONLY_VAR_SQLTEXT,
                sqlInfo.getErrMessage() ));
        (void)sqlInfo.fini();
    }
    IDE_EXCEPTION_END;

    return IDE_FAILURE;
}

IDE_RC qsvProcStmts::getException(
    qcStatement     * aStatement,
    qsExceptions    * aException)
{
    qsAllVariables      * sCurrVar;
    qsLabels            * sLabel;
    qsVariableItems     * sVariableItem;
    idBool                sFind = ID_FALSE;
    qcuSqlSourceInfo      sqlInfo;
    qsPkgParseTree      * sPkgParseTree;         // PROJ-1073 Package

    sPkgParseTree = aStatement->spvEnv->createPkg;

    // local exception variable
    // search user defined exception name
    for (sCurrVar = aStatement->spvEnv->allVariables;
         sCurrVar != NULL && sFind == ID_FALSE;
         sCurrVar = sCurrVar->next)
    {
        if (QC_IS_NULL_NAME(aException->labelNamePos) != ID_TRUE)
        {
            // label_name.exception_name
            for (sLabel = sCurrVar->labels;
                 sLabel != NULL && sFind == ID_FALSE;
                 sLabel = sLabel->next)
            {
                if (idlOS::strMatch(
                        sLabel->namePos.stmtText + sLabel->namePos.offset,
                        sLabel->namePos.size,
                        aException->labelNamePos.stmtText + aException->labelNamePos.offset,
                        aException->labelNamePos.size) == 0)
                {
                    for (sVariableItem = sCurrVar->variableItems;
                         sVariableItem != NULL && sFind == ID_FALSE;
                         sVariableItem = sVariableItem->next)
                    {
                        if (sVariableItem->itemType == QS_EXCEPTION &&
                            idlOS::strMatch(
                                sVariableItem->name.stmtText +
                                sVariableItem->name.offset,
                                sVariableItem->name.size,
                                aException->exceptionNamePos.stmtText +
                                aException->exceptionNamePos.offset,
                                aException->exceptionNamePos.size) == 0)
                        {
                            aException->id =
                                ((qsExceptionDefs *)sVariableItem)->id;
                            aException->objectID = QS_EMPTY_OID;
                            /* BUG-41240 EXCEPTION_INIT Pragma */ 
                            aException->errorCode =
                                ((qsExceptionDefs *)sVariableItem)->errorCode;
                            aException->userErrorCode =
                                ((qsExceptionDefs *)sVariableItem)->userErrorCode;
                            sFind = ID_TRUE;
                            break;
                        }
                    }
                }
            }
        }
        else
        {
            // exception declated in procedure
            for (sVariableItem = sCurrVar->variableItems;
                 sVariableItem != NULL && sFind == ID_FALSE;
                 sVariableItem = sVariableItem->next)
            {
                if (sVariableItem->itemType == QS_EXCEPTION &&
                    idlOS::strMatch(
                        sVariableItem->name.stmtText +
                        sVariableItem->name.offset,
                        sVariableItem->name.size,
                        aException->exceptionNamePos.stmtText +
                        aException->exceptionNamePos.offset,
                        aException->exceptionNamePos.size) == 0)
                {
                    aException->id =
                        ((qsExceptionDefs *)sVariableItem)->id;
                    aException->objectID = QS_EMPTY_OID;
                    /* BUG-41240 EXCEPTION_INIT Pragma */
                    aException->errorCode =
                        ((qsExceptionDefs *)sVariableItem)->errorCode;
                    aException->userErrorCode =
                        ((qsExceptionDefs *)sVariableItem)->userErrorCode;
                    sFind = ID_TRUE;
                    break;
                }
            }
        }
    }

    /* package body�� initialize section���� exception handling�� �����ϴ�. */
    if( (sFind == ID_FALSE) && (sPkgParseTree != NULL ) )
    {
        /* BUG-41847 
           package local�� ����� exception ������ ã�´�. */
        IDE_TEST( qsvPkgStmts::getPkgLocalException( aStatement,
                                                     aException,
                                                     &sFind )
                  != IDE_SUCCESS );
    }
    else
    {
        // Nothing to do.
    }

    if ( sFind == ID_FALSE )
    {
        IDE_TEST( getExceptionFromPkg( aStatement, aException, &sFind ) != IDE_SUCCESS );

        if( sFind == ID_FALSE )
        {
            // search system defined exception name
            if (QC_IS_NULL_NAME(aException->labelNamePos) != ID_TRUE)
            {
                if (idlOS::strMatch(
                        "STANDARD",
                        8,
                        aException->labelNamePos.stmtText + aException->labelNamePos.offset,
                        aException->labelNamePos.size) == 0)
                {
                    if (qsxUtil::getSystemDefinedException(
                            aStatement->myPlan->stmtText,
                            & aException->exceptionNamePos,
                            & aException->id,
                            & aException->errorCode)
                        != IDE_SUCCESS)
                    {
                        sFind = ID_FALSE;
                        IDE_CLEAR();
                    }
                    else
                    {
                        aException->isSystemDefinedError = ID_TRUE;
                        aException->objectID = QS_EMPTY_OID;
                        sFind = ID_TRUE;
                    }
                }
            }
            else
            {
                if (qsxUtil::getSystemDefinedException(
                        aStatement->myPlan->stmtText,
                        & aException->exceptionNamePos,
                        & aException->id,
                        & aException->errorCode)
                    != IDE_SUCCESS)
                {
                    sFind = ID_FALSE;
                    IDE_CLEAR();
                }
                else
                {
                    aException->isSystemDefinedError = ID_TRUE;
                    aException->objectID = QS_EMPTY_OID;
                    sFind = ID_TRUE;
                }
            }
        }
    }

    if (sFind == ID_FALSE)
    {
        sqlInfo.setSourceInfo( aStatement,
                               &aException->exceptionNamePos );
        IDE_RAISE(ERR_NOT_EXIST_EXCEPTION);
    }

    return IDE_SUCCESS;

  
    IDE_EXCEPTION(ERR_NOT_EXIST_EXCEPTION);
    {
        (void)sqlInfo.init(aStatement->qmeMem);
        IDE_SET(
            ideSetErrorCode(qpERR_ABORT_QSV_PROC_NOT_EXIST_EXCEPTION_SQLTEXT,
                            sqlInfo.getErrMessage() ));
        (void)sqlInfo.fini();
    }
    IDE_EXCEPTION_END;

    return IDE_FAILURE;
}

IDE_RC qsvProcStmts::connectAllVariables(
    qcStatement          * aStatement,
    qsLabels             * aLabels,
    qsVariableItems      * aVariableItems,
    idBool                 aInLoop,
    qsAllVariables      ** aOldAllVariables)
{
    qsAllVariables      * sCurrAllVariable;

    // save current pointer for disconnecting
    *aOldAllVariables = aStatement->spvEnv->allVariables;

    // make
    IDE_TEST(STRUCT_ALLOC(QC_QME_MEM(aStatement), qsAllVariables, &sCurrAllVariable)
             != IDE_SUCCESS);

    sCurrAllVariable->labels        = aLabels;
    sCurrAllVariable->variableItems = aVariableItems;
    sCurrAllVariable->inLoop        = aInLoop;

    // connect
    sCurrAllVariable->next = aStatement->spvEnv->allVariables;
    aStatement->spvEnv->allVariables = sCurrAllVariable;

    return IDE_SUCCESS;

  
    IDE_EXCEPTION_END;

    return IDE_FAILURE;
}

void qsvProcStmts::disconnectAllVariables(
    qcStatement          * aStatement,
    qsAllVariables       * aOldAllVariables)
{
    aStatement->spvEnv->allVariables = aOldAllVariables;
}

IDE_RC
qsvProcStmts::checkTriggerActionBody( qcStatement * aStatement,
                                      qsProcStmts * aProcStmts )
{
/***********************************************************************
 *
 * Description :
 *    CREATE TRIGGER�� ���� Action Body�� ���� Validation.
 *
 * Implementation :
 *    Trigger�� ���� ������ ���� ���� ������ �˻��Ѵ�.
 *        - Transaction Control Statement�� ����� ��.
 *        - PSM�� ȣ���ؼ��� �ȵ�.
 *
 *    DML Statement�� ��� �����ϴ� Table����� �����Ѵ�.
 *
 ***********************************************************************/

    qmmInsParseTree       * sInsParseTree;
    qsModifiedTable       * sModifiedTable;
    qsProcStmtSql         * sSQL;
    UInt                    sTableID;
    UInt                    sTableIDforMove = 0;
    idBool                  sIsDML;
    qcuSqlSourceInfo        sqlInfo;

    //---------------------------------------
    // ���ռ� �˻�
    //---------------------------------------

    IDE_DASSERT( aStatement != NULL );
    IDE_DASSERT( aProcStmts != NULL );

    //---------------------------------------
    // Trigger Action Body���� Statement�� ���� Validation
    //---------------------------------------

    sSQL = (qsProcStmtSql *)aProcStmts;
    sIsDML = ID_FALSE;

    // Action Body�� DML�� �����ϴ� Table�� List�� �����Ѱ�.
    // Cycle Detection�� ���� ������ �����Ѵ�.
    switch ( sSQL->common.stmtType )
    {
        //---------------------------------------
        // Transaction Control ������ ������ �� ����.
        // (COMMIT, ROLLBACK, SAVEPOINT, SET_TX) etc
        // PSM ȣ���� �� �� ����.
        //---------------------------------------

        case QS_PROC_STMT_COMMIT:
        case QS_PROC_STMT_ROLLBACK:
        case QS_PROC_STMT_SAVEPOINT:
        case QS_PROC_STMT_SET_TX:
            if ( QSV_CHECK_AT_TRIGGER_ACTION_BODY_BLOCK( aStatement->spvEnv->createProc )
                 == ID_FALSE )
            {
                sqlInfo.setSourceInfo( aStatement,
                                       &sSQL->common.pos );
                IDE_RAISE( err_invalid_statement_in_action_body );
            }
            else
            {
                // Nothing to do.
            }
            break;

            //---------------------------------------
            // DML Statement�� ��� Trigger�� Cycle�� �˻��ϱ� ����
            // ���� Table�� ����� �����Ѵ�.
            // ���� Ȯ���� ���� ���� ���̺� ���δ� �˻����� �ʴ´�.
            // ���� ���, INSERT event�� UPDATE ���������� Cycle�� ��������
            // ���� �� �ֱ� �����̴�.
            //---------------------------------------

        case QS_PROC_STMT_INSERT:
            sTableID = ((qmmInsParseTree *)sSQL->statement->myPlan->parseTree)
                ->tableRef->tableInfo->tableID;
            sIsDML = ID_TRUE;
            break;
        case QS_PROC_STMT_UPDATE:
            sTableID = ((qmmUptParseTree *)sSQL->statement->myPlan->parseTree)
                ->querySet->SFWGH->from->tableRef->tableInfo->tableID;
            sIsDML = ID_TRUE;
            break;
        case QS_PROC_STMT_DELETE:
            sTableID = ((qmmDelParseTree *)sSQL->statement->myPlan->parseTree)
                ->querySet->SFWGH->from->tableRef->tableInfo->tableID;
            sIsDML = ID_TRUE;
            break;
            // fix BUG-11861
            // move dml�� ���ŵǴ� table�� target, source 2���̹Ƿ�
            // sTableID���� target��, sTableIDforMove���� source�� ����
        case QS_PROC_STMT_MOVE:
            sTableID = ((qmmMoveParseTree *)sSQL->statement->myPlan->parseTree)
                ->querySet->SFWGH->from->tableRef->tableInfo->tableID;
            sTableIDforMove = ((qmmMoveParseTree *)sSQL->statement->myPlan->parseTree)
                ->targetTableRef->tableInfo->tableID;
            sIsDML = ID_TRUE;
            break;
        case QS_PROC_STMT_MERGE:
            sTableID = ((qmmMergeParseTree *)sSQL->statement->myPlan->parseTree)
                ->selectTargetParseTree
                ->querySet->SFWGH->from->tableRef->tableInfo->tableID;
            sIsDML = ID_TRUE;
            break;

        default:
            // Nothing To Do
            break;
    }

    if ( sIsDML == ID_TRUE )
    {
        IDE_TEST ( QC_QME_MEM(aStatement)->alloc( ID_SIZEOF(qsModifiedTable ),
                                                  (void**) & sModifiedTable )
                   != IDE_SUCCESS );

        sModifiedTable->tableID  = sTableID;
        sModifiedTable->stmtType = sSQL->common.stmtType;
        sModifiedTable->next     = aStatement->spvEnv->modifiedTableList;

        aStatement->spvEnv->modifiedTableList = sModifiedTable;

        // fix BUG-11861
        // move dml�� ��� insert,delete,update�� �޸�
        // modifiedtable�� �ΰ���(target table, source table)
        if( sSQL->common.stmtType == QS_PROC_STMT_MOVE )
        {
            IDE_TEST ( QC_QME_MEM(aStatement)->alloc( ID_SIZEOF(qsModifiedTable ),
                                                      (void**) & sModifiedTable )
                       != IDE_SUCCESS );

            sModifiedTable->tableID  = sTableIDforMove;
            sModifiedTable->stmtType = sSQL->common.stmtType;
            sModifiedTable->next     = aStatement->spvEnv->modifiedTableList;

            aStatement->spvEnv->modifiedTableList = sModifiedTable;
        }
        else
        {
            // Nothing to do
        }
        
        // BUG-36596 multi-table insert
        if( sSQL->common.stmtType == QS_PROC_STMT_INSERT )
        {
            sInsParseTree = (qmmInsParseTree *)sSQL->statement->myPlan->parseTree;

            if ( (sInsParseTree->flag & QMM_MULTI_INSERT_MASK) == QMM_MULTI_INSERT_TRUE )
            {
                // ó�� insert�� �̹� ����ߴ�.
                for ( sInsParseTree = sInsParseTree->next;
                      sInsParseTree != NULL;
                      sInsParseTree = sInsParseTree->next )
                {
                    sTableID = sInsParseTree->tableRef->tableInfo->tableID;

                    IDE_TEST ( QC_QME_MEM(aStatement)->alloc( ID_SIZEOF(qsModifiedTable ),
                                                              (void**) & sModifiedTable )
                               != IDE_SUCCESS );
                    
                    sModifiedTable->tableID  = sTableID;
                    sModifiedTable->stmtType = sSQL->common.stmtType;
                    sModifiedTable->next     = aStatement->spvEnv->modifiedTableList;
                    
                    aStatement->spvEnv->modifiedTableList = sModifiedTable;
                }
            }
            else
            {
                // Nothing To Do
            }
        }
        else
        {
            // Nothing To Do
        }
    }
    else
    {
        // Nothing To Do
    }

    return IDE_SUCCESS;

  
    IDE_EXCEPTION(err_invalid_statement_in_action_body);
    {
        (void)sqlInfo.init(aStatement->qmeMem);
        IDE_SET(
            ideSetErrorCode( qpERR_ABORT_TRIGGER_INVALID_ACTION_STMT,
                             sqlInfo.getErrMessage() ));
        (void)sqlInfo.fini();
    }
    IDE_EXCEPTION_END;

    return IDE_FAILURE;
}

IDE_RC qsvProcStmts::setLabelToParentStmt( qcStatement * aStatement,
                                           qsProcStmts * aParentStmt,
                                           qsLabels    * aLabel )
{
/***********************************************************************
 *
 * Description : PROJ-1335, To fix BUG-12475
 *               parent Statement�� label�� �޾���
 *               goto�� parent statement�� ���󰡸鼭 label�� �˻���
 *               �� ����.
 * Implementation :
 *   (1) parentStmt�� label �߿� �޾��� label�� �̸��� ������ ����
 *   (2) label�� �����Ͽ� parent->childLabels�� �޾Ƴ��´�.
 *       label�� �����ϴ� ������ parentLabels���� label�� �޸��� ������
 *       label->next�� �޶����� �����̴�.
 *
 ***********************************************************************/

    qsLabels * sParentLabel;
    qsLabels * sNewLabel;
    qcuSqlSourceInfo sqlInfo;

    IDE_DASSERT( aParentStmt != NULL );

    for( sParentLabel = aParentStmt->childLabels;
         sParentLabel != NULL;
         sParentLabel = sParentLabel->next )
    {
        if ( QC_IS_NAME_CASELESS_MATCHED( sParentLabel->namePos, aLabel->namePos ) )
        {
            sqlInfo.setSourceInfo( aStatement,
                                   &aLabel->namePos );
            IDE_RAISE( ERR_DUP_LABEL_NAME );
        }
        else
        {
            // Nothing to do.
        }
    }

    IDE_TEST(STRUCT_ALLOC(QC_QMP_MEM(aStatement), qsLabels, &sNewLabel)
             != IDE_SUCCESS);
    sNewLabel->namePos = aLabel->namePos;
    sNewLabel->id      = aLabel->id;
    sNewLabel->stmt    = aLabel->stmt;
    sNewLabel->next    = aParentStmt->childLabels;

    aParentStmt->childLabels = sNewLabel;

    return IDE_SUCCESS;

  
    IDE_EXCEPTION(ERR_DUP_LABEL_NAME);
    {
        (void)sqlInfo.init(aStatement->qmeMem);
        IDE_SET( ideSetErrorCode( qpERR_ABORT_QSV_DUPLICATE_LABEL,
                                  sqlInfo.getErrMessage() ) );
        (void)sqlInfo.fini();
    }
    IDE_EXCEPTION_END;

    return IDE_FAILURE;
}

idBool
qsvProcStmts::isSQLType( qsProcStmtType aType )
{
    idBool sRet;
    
    sRet = isDMLType( aType );

    if( sRet == ID_FALSE )
    {
        sRet = isFetchType( aType );
    }
    else
    {
        // Nothing to do.
    }

    return sRet;
}

/* BUG-24383 Support enqueue statement at PSM
 * QS_PROC_STMT_ENQUEUE TYPE �߰� */
/* BUG-37797 Support dequeue statement at PSM
 * QS_PROC_STMT_DEQUEUE TYPE �߰� */
idBool
qsvProcStmts::isDMLType( qsProcStmtType aType )
{
    if( ( aType == QS_PROC_STMT_INSERT ) ||
        ( aType == QS_PROC_STMT_DELETE ) ||
        ( aType == QS_PROC_STMT_MOVE )   ||
        ( aType == QS_PROC_STMT_MERGE )  ||
        ( aType == QS_PROC_STMT_UPDATE ) ||
        ( aType == QS_PROC_STMT_ENQUEUE )||
        ( aType == QS_PROC_STMT_DEQUEUE ) )
    {
        return ID_TRUE;
    }
    else
    {
        return ID_FALSE;
    }
}

/* BUG-37797 Support dequeue statement at PSM
 * QS_PROC_STMT_DEQUEUE TYPE �߰� */
idBool
qsvProcStmts::isFetchType( qsProcStmtType aType )
{
    if( ( aType == QS_PROC_STMT_SELECT ) ||
        ( aType == QS_PROC_STMT_DEQUEUE ) )
    {
        return ID_TRUE;
    }
    else
    {
        return ID_FALSE;
    }
}

/* PROJ-2197 PSM Renewal
 * BUG-36988 Query Trans
 * PSM ������ qsUsingParam�� �߰��Ѵ�. */
IDE_RC qsvProcStmts::makeUsingParam( qsVariables * aVariable,
                                     qtcNode     * aQtcColumn,
                                     mtcCallBack * aCallBack )
{
    qtcCallBackInfo   * sCallBackInfo;
    qsProcStmtSql     * sSql;
    qsUsingParam      * sUsingParam;
    qsUsingParam      * sTempUsingParam;
    iduVarMemList     * sMemory;
    const mtdModule   * sModule;
    mtcNode           * sMtcNode;

    SInt    sOffset;

    sCallBackInfo = (qtcCallBackInfo*)aCallBack->info;

    sSql  = ((qsProcStmtSql*)(sCallBackInfo->statement->spvEnv->currStmt));

    IDE_TEST_CONT( sSql == NULL, skip_make_usingparam );

    // BUG-38099
    IDE_TEST_CONT( qsvProcStmts::isSQLType(sSql->common.stmtType) != ID_TRUE, 
                    skip_make_usingparam );

    // BUG-37303
    if( aQtcColumn->position.stmtText != sSql->common.pos.stmtText )
    {
        sOffset = aQtcColumn->position.offset + aQtcColumn->position.stmtText - sSql->common.pos.stmtText;
    }
    else
    {
        sOffset = aQtcColumn->position.offset;
    }

    IDE_ERROR( sOffset > 0 );

    sMemory = QC_QMP_MEM( sCallBackInfo->statement );

    if( (aQtcColumn->lflag & QTC_NODE_COLUMN_CONVERT_MASK) == 
        QTC_NODE_COLUMN_CONVERT_TRUE )
    {
        aQtcColumn->lflag &= ~QTC_NODE_COLUMN_CONVERT_MASK;
        aQtcColumn->lflag |= QTC_NODE_COLUMN_CONVERT_FALSE;

        IDE_DASSERT( (aQtcColumn->position.size   > 0) &&
                     (aQtcColumn->position.offset > 0) );

        sTempUsingParam = sSql->usingParams;

        /* BUG-37042
         * ������ ��ġ�� PSM ������ �� �� �ִ�. ( case ... when �� ��� )
         * ������ ��ġ�� PSM �����̸� skip �ϵ��� �Ѵ�. */
        while( sTempUsingParam != NULL )
        {
            if( sTempUsingParam->paramNode->position.offset == sOffset )
            {
                IDE_CONT( skip_make_usingparam );
            }
            else
            {
                sTempUsingParam = sTempUsingParam->next;
            }
        }

        IDE_TEST( STRUCT_ALLOC(sMemory, qsUsingParam, &sUsingParam)
                  != IDE_SUCCESS );

        IDE_TEST( qtc::cloneQTCNodeTree( sMemory,
                                         aQtcColumn,
                                         &(sUsingParam->paramNode),
                                         ID_FALSE,
                                         ID_FALSE,
                                         ID_TRUE,
                                         ID_TRUE )
                  != IDE_SUCCESS );

        if( aQtcColumn->position.stmtText != sSql->common.pos.stmtText )
        {
            sUsingParam->paramNode->position.offset   = sOffset;
            sUsingParam->paramNode->position.stmtText = sSql->common.pos.stmtText;
        }
        else
        {
            // Nothing to do.
        }

        sUsingParam->paramNode->lflag &= ~QTC_NODE_PROC_VAR_MASK;
        sUsingParam->paramNode->lflag |= QTC_NODE_PROC_VAR_EXIST;

        sMtcNode = &(sUsingParam->paramNode->node);

        // PROJ-1073 Package
        IDE_DASSERT( aQtcColumn->node.orgNode == NULL );
        aQtcColumn->node.orgNode = (mtcNode *)sUsingParam->paramNode;

        if( sMtcNode->arguments != NULL )
        {
            // typeInfo�� ù��° �÷��� index column��.
            sModule = aVariable->typeInfo->columns->basicInfo->module;

            IDE_TEST( qtc::estimate( (qtcNode*)(sMtcNode->arguments),
                                     sCallBackInfo->tmplate,
                                     sCallBackInfo->statement,
                                     NULL,
                                     NULL,
                                     NULL )
                      != IDE_SUCCESS );

            IDE_TEST( mtf::makeConversionNodes( sMtcNode,
                                                sMtcNode->arguments,
                                                &sCallBackInfo->tmplate->tmplate,
                                                sCallBackInfo->tmplate->tmplate.stack,
                                                aCallBack,
                                                &sModule )
                      != IDE_SUCCESS );
        }

        sUsingParam->paramNode[0].lflag &= ~QTC_NODE_LVALUE_MASK;
        sUsingParam->paramNode[0].lflag |= QTC_NODE_LVALUE_ENABLE;

        // BUG-36131
        sUsingParam->inOutType = QS_IN;

        sUsingParam->next = NULL;

        if( sSql->usingParams == NULL )
        {
            sSql->usingParams = sUsingParam;
        }
        else
        {
            /* ���� ���� usingParam�� offset ������� �ִ´�. */
            sTempUsingParam = sSql->usingParams;

            if( (sTempUsingParam->paramNode->position.offset) >
                (sUsingParam->paramNode->position.offset) )
            {
                sUsingParam->next = sTempUsingParam;
                sSql->usingParams = sUsingParam;
            }
            else
            {
                while( sTempUsingParam->next != NULL )
                {
                    if( (sTempUsingParam->next->paramNode->position.offset) >
                        (sUsingParam->paramNode->position.offset) )
                    {
                        sUsingParam->next     = sTempUsingParam->next;
                        sTempUsingParam->next = sUsingParam;
                        break;
                    }
                    else
                    {
                        // Nothing to do
                    }
                    sTempUsingParam = sTempUsingParam->next;
                }

                if( sTempUsingParam->next == NULL )
                {
                    sTempUsingParam->next = sUsingParam;
                }
            }
        }
        sSql->usingParamCount++;
    }

    IDE_EXCEPTION_CONT( skip_make_usingparam );

    return IDE_SUCCESS;

  
    IDE_EXCEPTION_END;

    return IDE_FAILURE;
}

// PROJ-1073 Package
IDE_RC qsvProcStmts::getExceptionFromPkg( qcStatement * aStatement,
                                          qsExceptions * aException,
                                          idBool * aFind )
{
    qsExceptions     * sException = aException;
    qsOID              sPkgOID;
    UInt               sPkgUserID;
    qsxPkgInfo       * sPkgInfo;
    idBool             sExists = ID_FALSE;
    qcmSynonymInfo     sSynonymInfo;

    IDE_TEST( qcmSynonym::resolvePkg( aStatement,
                                      sException->userNamePos,
                                      sException->labelNamePos,
                                      &sPkgOID,
                                      &sPkgUserID,
                                      &sExists,
                                      &sSynonymInfo )
              != IDE_SUCCESS );

    if( sExists == ID_TRUE )
    {
        // synonym���� �����Ǵ� pkg�� ���
        IDE_TEST( qsvPkgStmts::makePkgSynonymList(
                           aStatement,
                           &sSynonymInfo,
                           sException->userNamePos,
                           sException->labelNamePos,
                           sPkgOID )
                  != IDE_SUCCESS );

        IDE_TEST(qsvPkgStmts::makeRelatedObjects(
                aStatement,
                & sException->userNamePos,
                & sException->labelNamePos,
                & sSynonymInfo,
                0,
                QS_PKG )
            != IDE_SUCCESS );

        IDE_TEST(qsxRelatedProc::prepareRelatedPlanTree(
                aStatement,
                sPkgOID,
                QS_PKG,
                &(aStatement->spvEnv->procPlanList))
            != IDE_SUCCESS);

        IDE_TEST( qsxPkg::getPkgInfo( sPkgOID,
                                      &sPkgInfo )
                  != IDE_SUCCESS);

        /* BUG-45164 */
        IDE_TEST_RAISE( sPkgInfo->isValid != ID_TRUE, err_object_invalid );

        IDE_TEST( qdpRole::checkDMLExecutePSMPriv(aStatement,
                                                  sPkgInfo->planTree->userID,
                                                  sPkgInfo->privilegeCount,
                                                  sPkgInfo->granteeID,
                                                  ID_FALSE,
                                                  NULL,
                                                  NULL)
                  != IDE_SUCCESS );

        IDE_TEST( searchExceptionFromPkg( sPkgInfo,
                                          sException,
                                          aFind ) != IDE_SUCCESS );
    }
    else
    {
        // Nothing to do.
    }

    return IDE_SUCCESS;

    IDE_EXCEPTION( err_object_invalid );   /* BUG-45164 */ 
    {
        IDE_SET( ideSetErrorCode(qpERR_ABORT_QSX_PLAN_INVALID) );
    }
    IDE_EXCEPTION_END;

    return IDE_FAILURE;
}

IDE_RC qsvProcStmts::searchExceptionFromPkg( qsxPkgInfo   * aPkgInfo,
                                             qsExceptions * aException,
                                             idBool       * aFind )
{
    qsPkgParseTree  * sPlanTree;
    qsPkgStmtBlock  * sBLOCK;
    qsVariableItems * sPkgVariable;
    qsExceptions    * sException = aException;

    sPlanTree = aPkgInfo->planTree;
    sBLOCK = sPlanTree->block;

    IDE_DASSERT( sException != NULL );
    IDE_DASSERT( QC_IS_NULL_NAME( sException->exceptionNamePos ) == ID_FALSE );

    for( sPkgVariable = sBLOCK->variableItems ;
         sPkgVariable != NULL;
         sPkgVariable = sPkgVariable->next )
    {
        if( sPkgVariable->itemType == QS_EXCEPTION )
        {
            if(  idlOS::strMatch( sPkgVariable->name.stmtText + sPkgVariable->name.offset,
                                  sPkgVariable->name.size,
                                  sException->exceptionNamePos.stmtText + sException->exceptionNamePos.offset,
                                  sException->exceptionNamePos.size) == 0 )
            {
                sException->id = ( ( qsExceptionDefs * )sPkgVariable )->id;
                sException->objectID = aPkgInfo->pkgOID;
                /* BUG-41240 EXCEPTION_INIT Pragma */
                sException->errorCode = ( ( qsExceptionDefs * )sPkgVariable )->errorCode;
                aException->userErrorCode = ((qsExceptionDefs *)sPkgVariable)->userErrorCode;
                *aFind = ID_TRUE;
                break;
            }
            else
            {
                *aFind = ID_FALSE;
            }
        }
        else
        {
            *aFind = ID_FALSE;
        }
    }

    return IDE_SUCCESS;
    
}

/* BUG-36902 
 * qsxExecutor::execStmtList �Լ����� qtc::setDatePseudoColumn �Լ���
 * ȣ���� �� ���θ� useDate�� �����Ѵ�.
 * PSM ���� DML�� �� DML�� ������ �� qtc::setDatePseudoColumn �Լ��� ȣ���ϹǷ� 
 * useDate�� �������� �ʴ´�. */
IDE_RC qsvProcStmts::setUseDate( qsvEnvInfo * aEnvInfo )
{
    qsProcParseTree * sProcParseTree;
    qsProcStmts     * sProcStmts;

    sProcParseTree = aEnvInfo->createProc;
    sProcStmts     = aEnvInfo->currStmt;

    /* BUG-37854
       parameter�� default ������ sysdate�� ���,
       parameter�� validate ���������� ���� qsProcStmts�� NULL�̴�. */
    if ( sProcStmts != NULL )
    {
        if ( isSQLType( sProcStmts->stmtType ) == ID_FALSE )
        {
            sProcStmts->useDate = ID_TRUE;
        }
        else
        {
            // Nothing to do.
        }
    }
    else
    {
        IDE_DASSERT( sProcParseTree != NULL );
        sProcParseTree->paramUseDate = ID_TRUE;
    }

    return IDE_SUCCESS;
    
}

/* BUG-36988 Query Transformation function for PSM is required. */
IDE_RC qsvProcStmts::queryTrans ( qcStatement * aQcStmt,
                                  qsProcStmts * aProcStmts )
{
    qsProcStmtSql      * sSql;
    qcNamePosition       sSqlPos;

    qsUsingParam       * sUsingParam;
    SInt                 sUsingOffset;
    SInt                 sIntoOffset;
    SInt                 sOrgSqlTextOffset;
    SInt                 sVarOffset;

    iduVarMemString      sSqlBuffer;
    SInt                 sStrLen;
    SInt                 sTotalLen  = 0;

    // ������ Bind ������ üũ�ϱ� ���� list
    qcNamePosition     * sBindVars  = NULL;
    SInt                 sBindCount = 0;
    UInt                 sState     = 0;

    // BUG-37333
    qsUsingSubprograms * sSubprogram;
    SInt                 sSubprogramOffset = 0;
    qcNamePosition     * sPkgName          = NULL;

    sSql        = (qsProcStmtSql *)aProcStmts;
    sSqlPos     = sSql->common.pos;
    sUsingParam = sSql->usingParams;
    sSubprogram = sSql->usingSubprograms;

    sOrgSqlTextOffset = sSqlPos.offset;

    IDE_TEST( iduVarMemStringInitialize( &sSqlBuffer, QC_QME_MEM(aQcStmt), QSV_SQL_BUFFER_SIZE )
              != IDE_SUCCESS );

    if( sSql->usingParamCount > 0 )
    {
        /* ���� �̸��� PSM ������ ���� �̸��� bind ������ ��ȯ�ϱ� ���ؼ�
         * sBindVars�� ��ȯ�� PSM ������ �̸��� �����Ѵ�. */
        IDE_TEST( STRUCT_ALLOC_WITH_COUNT( QC_QME_MEM( aQcStmt ),
                                           qcNamePosition,
                                           sSql->usingParamCount,
                                           &sBindVars )
                  != IDE_SUCCESS );
        sState = 1;
    }
    else
    {
        // Nothing to do.
    }

    if( sSql->intoVariables != NULL )
    {
        sIntoOffset = sSql->intoVariables->intoPosition.offset;
    }
    else
    {
        /* into ���� ���� ��� (select statement�� �ƴѰ��)
         * into ���� �������� �ʱ����ؼ� ID_SINT_MAX�� �����Ѵ�. */
        sIntoOffset = ID_SINT_MAX;
    }

    if( aQcStmt->spvEnv->createPkg != NULL )
    {
        /* package subprogram list�� ���, ������ package ���� subprogram�� ������� ��,
         * ���� �� �ش� subprogram�� ã�� ���� package name�� ��������� �ϸ�,
         * create package�� validate �� �̷������.
         * ��, create package�� validate �� �ش� list�� ���������, package name�� �����Ѵ�. */
        sPkgName = &aQcStmt->spvEnv->createPkg->pkgNamePos;
    }
    else
    {
        // package�� �ƴ� ��� subprogramList�� �������� �ʱ� ���� ID_SINT_MAX�� ����
        IDE_DASSERT( sSubprogram == NULL );
        sSubprogramOffset = ID_SINT_MAX;
    }

    while( (sUsingParam != NULL) ||
           (sIntoOffset != ID_SINT_MAX) || 
           (sSubprogram != NULL) )
    {
        if( sUsingParam != NULL )
        {
            IDE_DASSERT( sUsingParam->paramNode != NULL );
            sUsingOffset = sUsingParam->paramNode->position.offset;
        }
        else
        {
            // sUsingParam�� NULL �̸� into�� �����ϱ� ����
            // offset�� ID_SINT_MAX�� �����Ѵ�.
            sUsingOffset = ID_SINT_MAX;
        }

        if( sSubprogram != NULL )
        {
            IDE_DASSERT( sSubprogram->subprogramNode != NULL );
            sSubprogramOffset = sSubprogram->subprogramNode->position.offset;
        }
        else
        {
            sSubprogramOffset = ID_SINT_MAX;
        }

        if( sUsingOffset != sIntoOffset )
        {
            sVarOffset = (sUsingOffset < sIntoOffset) ? sUsingOffset : sIntoOffset;
        }
        else
        {
            IDE_DASSERT( sUsingOffset == ID_SINT_MAX );
            sVarOffset = ID_SINT_MAX;
        }

        if( sSubprogramOffset != ID_SINT_MAX )
        {
            IDE_DASSERT( sSubprogramOffset != sVarOffset );
            sVarOffset = (sSubprogramOffset < sVarOffset) ? sSubprogramOffset : sVarOffset;
        }
        else
        {
            // Nothing to do.
        }

        // PSM ���� Ȥ�� into�� ������ SQL�� target buffer�� �����Ѵ�.
        sStrLen  = sVarOffset - sOrgSqlTextOffset;

        // BUG-39212 Rows referenced by trigger should be used like host variable.
        if ( ( sVarOffset > 0 ) && ( sVarOffset != ID_SINT_MAX ) )
        {
            if ( sSqlPos.stmtText[sVarOffset- 1] == ':' )
            {
                sStrLen--;
            }
            else
            {
                // Nothing to do.
            }
        }
        else
        {
            // Nothing to do.
        }

        IDE_ERROR( sStrLen > 0 );

        IDE_TEST( iduVarMemStringAppendLength( &sSqlBuffer, sSqlPos.stmtText + sOrgSqlTextOffset, sStrLen )
                  != IDE_SUCCESS );

        IDE_DASSERT( sVarOffset != ID_SINT_MAX );

        if( sVarOffset == sIntoOffset )
        {
            // BUG-36577 : from clause can be omitted.
            if( QC_IS_NULL_NAME( sSql->from->fromPosition ) == ID_TRUE )
            {
                sOrgSqlTextOffset = sSqlPos.offset + sSqlPos.size;
            }
            else
            {
                sOrgSqlTextOffset = sSql->from->fromPosition.offset;
            }

            sIntoOffset = ID_SINT_MAX;
        }
        else
        {
            if( sVarOffset == sUsingOffset )
            {
                IDE_ERROR( sSql->usingParamCount > 0);

                IDE_TEST( makeBindVar( aQcStmt,
                                       sSql,
                                       &sSqlBuffer,
                                       &sUsingParam,
                                       &sBindVars,
                                       &sBindCount,
                                       &sOrgSqlTextOffset )
                          != IDE_SUCCESS );
            }
            else
            {
                IDE_TEST( iduVarMemStringAppendLength( &sSqlBuffer,
                                                       sPkgName->stmtText + sPkgName->offset,
                                                       sPkgName->size )
                          != IDE_SUCCESS );

                IDE_TEST( iduVarMemStringAppendFormat( &sSqlBuffer, ".", 1 )
                          != IDE_SUCCESS );

                sOrgSqlTextOffset = sSubprogramOffset;

                sSubprogram = sSubprogram->next;
            }
        }
    }

    // ������ SQL�� target buffer�� �����Ѵ�.
    sStrLen  = (sSqlPos.offset + sSqlPos.size) - sOrgSqlTextOffset;

    if( sStrLen > 0 )
    {
        IDE_TEST( iduVarMemStringAppendLength( &sSqlBuffer, sSqlPos.stmtText + sOrgSqlTextOffset, sStrLen )
                  != IDE_SUCCESS );
    }
    else
    {
        IDE_ERROR( sStrLen == 0 );
    }

    sTotalLen = iduVarMemStringGetLength( &sSqlBuffer );

    // ��ȯ�� SQL text�� ���� �޸𸮸� �Ҵ��Ͽ� �����Ѵ�.
    IDE_TEST( QC_QMP_MEM(aQcStmt)->alloc( sTotalLen + 1,
                                          (void**)&sSql->sqlText )
              != IDE_SUCCESS );

    IDE_TEST( iduVarMemStringExtractString( &sSqlBuffer, sSql->sqlText, sTotalLen )
              != IDE_SUCCESS );
    sSql->sqlText[sTotalLen] = '\0';

    sSql->sqlTextLen = sTotalLen;

    IDE_TEST( iduVarMemStringFinalize( &sSqlBuffer )
              != IDE_SUCCESS );

    if( sState == 1 )
    {
        sState = 0;
        IDE_TEST( QC_QME_MEM(aQcStmt)->free(sBindVars)
                  != IDE_SUCCESS );
    }
    else
    {
        // Nothing to do.
    }

    return IDE_SUCCESS;

  
    IDE_EXCEPTION_END;

    if( sState == 1 )
    {
        (void)QC_QME_MEM(aQcStmt)->free(sBindVars);
    }
    else
    {
        // Nothing to do.
    }

    return IDE_FAILURE;
}

// BUG-36988
IDE_RC qsvProcStmts::makeBindVar( qcStatement     * aQcStmt,
                                  qsProcStmtSql   * aSql,
                                  iduVarMemString * aSqlBuffer,
                                  qsUsingParam   ** aUsingParam,
                                  qcNamePosition ** aBindVars,
                                  SInt            * aBindCount,
                                  SInt            * aOrgSqlTextOffset )
{
    qtcNode        * sUsingNode;
    qsUsingParam   * sUsingParam;
    SInt             i;
    UInt             sMtdModuleID; 
    qcTemplate     * sTemplate;
    mtcColumn      * sMtcColumn;

    // For Record Type
    qcmColumn      * sRowColumn = NULL;
    qtcModule      * sRowModule = NULL;
    SInt             sColumnCount;
    qsUsingParam   * sNewUsingParam;
    qtcNode        * sIndexNode[2];
    qcNamePosition   sNullPosition;
    // BUG-42858
    qcuSqlSourceInfo    sqlInfo;

    SET_EMPTY_POSITION( sNullPosition );

    sUsingParam = *aUsingParam;
    sUsingNode  = sUsingParam->paramNode;

    // BUG-42858
    // array�� index�� table�� column�� ����� �� �����ϴ�.
    if ( ( ( sUsingNode->lflag & QTC_NODE_SP_ARRAY_INDEX_VAR_MASK )
           == QTC_NODE_SP_ARRAY_INDEX_VAR_EXIST ) &&
         ( sUsingNode->depInfo.depCount > 0 ) )
    {
        sqlInfo.setSourceInfo( aQcStmt,
                               & ((qtcNode*)(sUsingNode->node.arguments))->position );
        IDE_RAISE( ERR_INVALID_IDENTIFIER );
    }
    else
    {
        // Nothing to do.
    }

    for( i = 0; i < *aBindCount; i++)
    {
        // �̹� ����� bind �������� ã�´�.
        if ( QC_IS_NAME_MATCHED( (*aBindVars)[i], sUsingNode->position ) )
        {
            break;
        }
        else
        {
            // Nothing to do.
        }
    }

    if( i == *aBindCount ) // ã�� ���� ��� ����Ѵ�.
    {
        (*aBindVars)[i] = sUsingNode->position;
        (*aBindCount)++;
    }
    else
    {
        // Noting to do.
    }

    sTemplate    = QC_SHARED_TMPLATE( aQcStmt );
    sMtcColumn   = QTC_TMPL_COLUMN(sTemplate, sUsingNode);
    sMtdModuleID = sMtcColumn->module->id;

    if( sMtdModuleID == MTD_ASSOCIATIVE_ARRAY_ID )
    {
        sRowColumn = ((qtcModule*)sMtcColumn->module)->typeInfo->columns->next;
        sRowModule = (qtcModule*)sRowColumn->basicInfo->module;

        // Primitive type�� associative array �� ���
        if( ( sRowModule->module.id != MTD_ROWTYPE_ID ) &&
            ( sRowModule->module.id != MTD_RECORDTYPE_ID ) )
        {
            IDE_TEST( qtc::makeProcVariable( aQcStmt,
                                             &(sUsingParam->paramNode),
                                             &sNullPosition,
                                             NULL,
                                             QTC_PROC_VAR_OP_NEXT_COLUMN )
                      != IDE_SUCCESS );

            IDE_TEST( mtc::initializeColumn(
                          QTC_TMPL_COLUMN( sTemplate, sUsingParam->paramNode ),
                          &mtdInteger,
                          0,
                          0,
                          0 )
                      != IDE_SUCCESS );

            sUsingParam->paramNode[0].lflag &= ~QTC_NODE_COLUMN_ESTIMATE_MASK;
            sUsingParam->paramNode[0].lflag |= QTC_NODE_COLUMN_ESTIMATE_TRUE;

            IDE_TEST( qtc::estimate( sUsingParam->paramNode,
                                     sTemplate,
                                     aQcStmt,
                                     NULL,
                                     NULL,
                                     NULL )
                      != IDE_SUCCESS );

            sUsingParam->paramNode[0].lflag &= ~QTC_NODE_LVALUE_MASK;
            sUsingParam->paramNode[0].lflag |= QTC_NODE_LVALUE_ENABLE;
        }
        else
        {
            // Record type�� ��� �ݵ�� ���� ������ usingParam�̴�.
            // (return .. into �������� ��밡���ϴ�.)
            IDE_DASSERT( sUsingParam->next == NULL );

            // record type ���� bind ������ �ִ´�.
            sMtdModuleID = sRowModule->module.id;
            sMtcColumn   = sRowColumn->basicInfo;
        }
    }
    else
    {
        // Noting to do.
    }

    // 1) Record/row type�� ���
    // 2) Record/row type�� Array type�� ���
    if( ( sMtdModuleID == MTD_ROWTYPE_ID ) ||
        ( sMtdModuleID == MTD_RECORDTYPE_ID ) )
    {
        sColumnCount = ((qtcModule*)sMtcColumn->module)->typeInfo->columnCount;

        // Record type�� ��� �ݵ�� ���� ������ usingParam�̴�.
        // (return .. into �������� ��밡���ϴ�.)
        IDE_DASSERT( sUsingParam->next == NULL );

/* Record Type�� ��� record�� column ������ŭ bind ������ �����.
 *    ex) TYPE ARR1 iS RECORD ( A1 INTEGER, A2 INTEGER );
 *        UPDATE T1 SET I1=I1, I2=I2 RETURN I1, I2 BULK COLLECT INTO ARR1;
 * step1) UPDATE T1 SET I1=I1, I2=I2 RETURN I1, I2 BULK COLLECT INTO :O0 
 * step2) UPDATE T1 SET I1=I1, I1=I2 RETURN I1, I2 BULK COLLECT INTO :O0, :O1 */
        for( i = 0; i < sColumnCount; i++ )
        {
            IDE_TEST( qtc::makeProcVariable( aQcStmt,
                                             sIndexNode,
                                             &sNullPosition,
                                             NULL,
                                             QTC_PROC_VAR_OP_NEXT_COLUMN )
                      != IDE_SUCCESS );

            IDE_TEST( mtc::initializeColumn(
                           QTC_TMPL_COLUMN( sTemplate, sIndexNode[0] ),
                           &mtdInteger,
                           0,
                           0,
                           0 )
                      != IDE_SUCCESS );

            sIndexNode[0]->lflag &= ~QTC_NODE_COLUMN_ESTIMATE_MASK;
            sIndexNode[0]->lflag |= QTC_NODE_COLUMN_ESTIMATE_TRUE;

            IDE_TEST( qtc::estimate( sIndexNode[0],
                                     sTemplate,
                                     aQcStmt,
                                     NULL,
                                     NULL,
                                     NULL )
                      != IDE_SUCCESS );

            sIndexNode[0]->lflag &= ~QTC_NODE_LVALUE_MASK;
            sIndexNode[0]->lflag |= QTC_NODE_LVALUE_ENABLE;

            // ó���̸� usingParam�� paramNode�� �����Ѵ�.
            // ���Ŀ��� usingParam�� �Ҵ��ؼ� �߰��Ѵ�.
            if( i == 0 )
            {
                sUsingParam->paramNode = sIndexNode[0];

                IDE_TEST( iduVarMemStringAppendFormat( aSqlBuffer, ":O%"ID_INT32_FMT, i )
                          != IDE_SUCCESS );
            }
            else
            {
                IDE_TEST( STRUCT_ALLOC(QC_QMP_MEM(aQcStmt),
                                       qsUsingParam,
                                       &sNewUsingParam)
                          != IDE_SUCCESS );

                sNewUsingParam->paramNode = sIndexNode[0];
                sNewUsingParam->next      = NULL;
                sNewUsingParam->inOutType = QS_IN;

                sUsingParam->next = sNewUsingParam;
                sUsingParam       = sNewUsingParam;

                aSql->usingParamCount++;

                IDE_TEST( iduVarMemStringAppendFormat( aSqlBuffer, ", :O%"ID_INT32_FMT, i )
                          != IDE_SUCCESS );
            }
        }
    }
    else
    {
        // PSM ������ bind ������ ġȯ�ؼ� target buffer�� �߰��Ѵ�.
        IDE_TEST( iduVarMemStringAppendFormat( aSqlBuffer, ":B%"ID_INT32_FMT, i )
                  != IDE_SUCCESS );
    }

    *aUsingParam       = sUsingParam->next;
    *aOrgSqlTextOffset = sUsingNode->position.offset + sUsingNode->position.size;

    return IDE_SUCCESS;

    IDE_EXCEPTION(ERR_INVALID_IDENTIFIER);
    {
        (void)sqlInfo.init(aQcStmt->qmeMem);
        IDE_SET(
            ideSetErrorCode( qpERR_ABORT_QSV_INVALID_IDENTIFIER,
                            sqlInfo.getErrMessage() ));
        (void)sqlInfo.fini();
    }
    IDE_EXCEPTION_END;

    return IDE_FAILURE;
}

// BUG-37501
// exception ���ο��� ����� label���� exception block�� childLabel�� �������ش�.
// �� childLabel���� goto������ label�� ã�� �� ���ȴ�.
IDE_RC qsvProcStmts::connectChildLabel( qcStatement * aStatement,
                                        qsProcStmts * aSrcProcStmt,
                                        qsProcStmts * aDestProcStmt )
{
    qsLabels * sNewLabel  = NULL;
    qsLabels * sSrcLabels = NULL;
    
    if( aSrcProcStmt->childLabels != NULL )
    {
        for( sSrcLabels = aSrcProcStmt->childLabels;
             sSrcLabels != NULL;
             sSrcLabels = sSrcLabels->next )
        {
            IDE_TEST(STRUCT_ALLOC(QC_QMP_MEM(aStatement), qsLabels, &sNewLabel)
                     != IDE_SUCCESS);

            idlOS::memcpy(sNewLabel,
                          sSrcLabels,
                          ID_SIZEOF(qsLabels));
            sNewLabel->next = NULL;

            if( aDestProcStmt->childLabels == NULL )
            {
                aDestProcStmt->childLabels = sNewLabel;
            }
            else
            {
                sNewLabel->next = aDestProcStmt->childLabels;
                aDestProcStmt->childLabels = sNewLabel;
            }
        }
    }
    else
    {
        // Nothing to do.
    }

    return IDE_SUCCESS;

  
    IDE_EXCEPTION_END;

    return IDE_FAILURE;
}

IDE_RC qsvProcStmts::validateIntoClause( qcStatement * aStatement,
                                         qmsTarget   * aTarget,
                                         qmsInto     * aIntoVars )
{
    qmsTarget        * sCurrTarget;
    UInt               sTargetCount     = 0;
    idBool             sExistsRecordVar = ID_FALSE;

    IDE_DASSERT( aIntoVars->intoNodes != NULL );

    // target ������ ���Ѵ�.
    for ( sCurrTarget = aTarget ;
          sCurrTarget != NULL ;
          sCurrTarget = sCurrTarget->next )
    {
        sTargetCount++;
    }

    // into clause , bulk collect into clause�� ���� validation�� �Ѵ�.
    IDE_TEST( validateIntoClauseInternal( aStatement,
                                          aIntoVars,
                                          sTargetCount,
                                          &sExistsRecordVar,
                                          ID_FALSE, /* aIsExecImm */
                                          ID_FALSE, /* alsRefCur */
                                          NULL /* aIntoVarCount */)
              != IDE_SUCCESS );

    return IDE_SUCCESS;

  
    IDE_EXCEPTION_END;

    return IDE_FAILURE;
}

/* BUG-41707 */
IDE_RC qsvProcStmts::validateIntoClauseForRefCursor( qcStatement * aStatement,
                                                     qsProcStmts * aProcStmts,
                                                     qmsInto     * aIntoVars )
{
    qsProcStmtFetch  * sProcStmtFetch   = (qsProcStmtFetch*)aProcStmts;    
    idBool             sExistsRecordVar = ID_FALSE;
    UInt               sIntoVarCount;

    IDE_DASSERT( aIntoVars->intoNodes != NULL );

    // into clause , bulk collect into clause�� ���� validation�� �Ѵ�.
    IDE_TEST( validateIntoClauseInternal( aStatement,
                                          aIntoVars,
                                          0, /* target count */
                                          &sExistsRecordVar,
                                          ID_FALSE, /* aIsExecImm */
                                          ID_TRUE, /* aIsRefCur */
                                          &sIntoVarCount )
              != IDE_SUCCESS );

    sProcStmtFetch->isIntoVarRecordType = sExistsRecordVar;
    sProcStmtFetch->intoVarCount = sIntoVarCount;

    return IDE_SUCCESS;

  
    IDE_EXCEPTION_END;

    return IDE_FAILURE;
}

IDE_RC qsvProcStmts::validateIntoClauseInternal( qcStatement * aStatement,
                                                 qmsInto     * aIntoVars,
                                                 UInt          aTargetCount,
                                                 idBool      * aExistsRecordVar,
                                                 idBool        aIsExecImm,
                                                 idBool        alsRefCur,
                                                 UInt        * aIntoVarCount )
{
/***********************************************************************
 *
 * Description : validate into clause.
 *    BUG-37273, BUG-41707
 *
 * Implementation :
 *    into clause , bulk collect into clause�� ���� validation�� �Ѵ�.
 *        - SELECT
 *        - EXECUTE IMMEDIATE
 *        - CURSOR
 *        - REF CURSOR
 * 
 ************************************************************************/

    qtcNode          * sIntoVars;
    UInt               sIntoVarCount    = 0;
    qtcNode          * sCurrIntoVar;
    idBool             sFindVar         = ID_FALSE;
    idBool             sExistsRecordVar = ID_FALSE;
    qsVariables      * sArrayVariable;
    mtcColumn        * sMtcColumn;
    qtcModule        * sQtcModule;
    qcmColumn        * sRowColumn;
    qtcModule        * sRowModule;
    qtcNode          * sIndexNode[2];
    qcNamePosition     sNullPosition;
    qcNamePosition     sPos;
    qcuSqlSourceInfo   sqlInfo;

    IDE_DASSERT( aIntoVars->intoNodes != NULL );

    sIntoVars     = aIntoVars->intoNodes;
    sPos.stmtText = aIntoVars->intoNodes->position.stmtText;
    sPos.offset   = aIntoVars->intoNodes->position.offset;
    sPos.size     = 0;

    for ( sCurrIntoVar = sIntoVars;
          sCurrIntoVar != NULL;
          sCurrIntoVar = (qtcNode *)(sCurrIntoVar->node.next) )
    {
        sPos.size = sCurrIntoVar->position.offset +
                    sCurrIntoVar->position.size -
                    sPos.offset;

        sIntoVarCount++;

        IDE_TEST( qsvProcVar::searchVarAndPara( aStatement,
                                                sCurrIntoVar,
                                                ID_TRUE, // for OUTPUT
                                                &sFindVar,
                                                &sArrayVariable )
                  != IDE_SUCCESS );

        if ( sFindVar == ID_FALSE )
        {
            IDE_TEST( qsvProcVar::searchVariableFromPkg( aStatement,
                                                         sCurrIntoVar,
                                                         &sFindVar,
                                                         &sArrayVariable )
                      != IDE_SUCCESS );
        }

        if ( sFindVar == ID_FALSE )
        {
            sqlInfo.setSourceInfo( aStatement,
                                   & sCurrIntoVar->position );
            IDE_RAISE(ERR_NOT_FOUND_VAR);
        }
        else
        {
            // Nothing to do.
        }

        /* lvalue�� psm������ �����ϹǷ� lvalue flag�� ����.
           qtcColumn ��⿡�� estimate�� ������. */
        sCurrIntoVar->lflag |= QTC_NODE_LVALUE_ENABLE;

        IDE_TEST( qtc::estimate( sCurrIntoVar,
                                 QC_SHARED_TMPLATE(aStatement),
                                 aStatement,
                                 NULL,
                                 NULL,
                                 NULL )
                  != IDE_SUCCESS );

        // BUG-42790 lvalue���� �׻� columnModule�̿��� �Ѵ�.
        IDE_ERROR_RAISE( ( sCurrIntoVar->node.module ==
                           &qtc::columnModule ),
                         ERR_UNEXPECTED_MODULE_ERROR );

        if ( ( sCurrIntoVar->lflag & QTC_NODE_OUTBINDING_MASK )
             == QTC_NODE_OUTBINDING_DISABLE )
        {
            sqlInfo.setSourceInfo( aStatement,
                                   & sCurrIntoVar->position );
            IDE_RAISE(ERR_INOUT_TYPE_MISMATCH);
        }
        else
        {
            // Nothing to do.
        }

        sMtcColumn = QTC_STMT_COLUMN( aStatement, sCurrIntoVar );

        if ( aIntoVars->bulkCollect == ID_TRUE )
        {
            if ( sMtcColumn->module->id == MTD_ASSOCIATIVE_ARRAY_ID )
            {
                if ( sCurrIntoVar->node.arguments == NULL )
                {
                    SET_EMPTY_POSITION( sNullPosition );

                    IDE_TEST( qtc::makeProcVariable( aStatement,
                                                     sIndexNode,
                                                     & sNullPosition,
                                                     NULL,
                                                     QTC_PROC_VAR_OP_NEXT_COLUMN )
                              != IDE_SUCCESS );

                    IDE_TEST( mtc::initializeColumn(
                            QC_SHARED_TMPLATE(aStatement)->tmplate.
                            rows[sIndexNode[0]->node.table].columns + sIndexNode[0]->node.column,
                            & mtdInteger,
                            0,
                            0,
                            0 )
                        != IDE_SUCCESS );

                    sIndexNode[0]->lflag &= ~QTC_NODE_COLUMN_ESTIMATE_MASK;
                    sIndexNode[0]->lflag |= QTC_NODE_COLUMN_ESTIMATE_TRUE;

                    sCurrIntoVar->lflag &= ~QTC_NODE_PROC_VAR_ESTIMATE_MASK;
                    sCurrIntoVar->lflag |= QTC_NODE_PROC_VAR_ESTIMATE_FALSE;

                    sCurrIntoVar->lflag &= ~QTC_NODE_SP_ARRAY_INDEX_VAR_MASK;
                    sCurrIntoVar->lflag |= QTC_NODE_SP_ARRAY_INDEX_VAR_EXIST;

                    // index node�� �����Ѵ�.
                    sCurrIntoVar->node.arguments = (mtcNode*) sIndexNode[0];
                    sCurrIntoVar->node.lflag |= 1;

                    IDE_TEST( qtc::estimate( sCurrIntoVar,
                                             QC_SHARED_TMPLATE(aStatement),
                                             aStatement,
                                             NULL,
                                             NULL,
                                             NULL )
                              != IDE_SUCCESS );

                    sCurrIntoVar->lflag |= QTC_NODE_LVALUE_ENABLE;
                }
                else
                {
                    // Nothing to do.
                }

                sQtcModule = (qtcModule*)sMtcColumn->module;

                sRowColumn = sQtcModule->typeInfo->columns->next;
                sRowModule = (qtcModule*)sRowColumn->basicInfo->module;

                if ( ( sRowModule->module.id == MTD_ROWTYPE_ID ) ||
                     ( sRowModule->module.id == MTD_RECORDTYPE_ID ) )
                {
                    sExistsRecordVar = ID_TRUE;
                }
                else
                {
                    // Nothing to do.
                }
            } /* ASSOCIATIVE ARRAY Type�� ��� */
            else
            {
                // bulk collect�� ��� associative array type�� ����
                sqlInfo.setSourceInfo(
                    aStatement,
                    & sCurrIntoVar->position );
                IDE_RAISE(ERR_NOT_ASSOCIATIVE);
            } /* ASSOCIATIVE ARRAY Type�� �ƴ� ��� */
        } /* BULK COLLECT INTO CLAUSE */
        else
        {
            if ( sMtcColumn->module->id == MTD_ASSOCIATIVE_ARRAY_ID )
            {
                // bulk collect�� ������ associative array type �Ұ�
                sqlInfo.setSourceInfo(
                    aStatement,
                    & sCurrIntoVar->position );
                IDE_RAISE(ERR_NOT_BULK_ASSOCIATIVE);
            }
            else
            {
                if ( ( sMtcColumn->module->id == MTD_ROWTYPE_ID ) ||
                     ( sMtcColumn->module->id == MTD_RECORDTYPE_ID ) )
                {
                    sExistsRecordVar = ID_TRUE;
                }
                else
                {
                    // Nothing to do.
                }
            }
        } /* INTO CLAUSE */
    } /* end for */

    if( sExistsRecordVar == ID_TRUE )
    {
        // record variable�� �ִ� ����̹Ƿ� targetCount�� record var�� �����÷�
        // count�� üũ�Ѵ�.
        // ��, �̶��� intoVarCount�� �ݵ�� 1�̾�߸� �Ѵ�.
        if( sIntoVarCount != 1 )
        {
            sqlInfo.setSourceInfo(
                aStatement,
                &sPos );
            IDE_RAISE(ERR_RECORD_COUNT);
        }
        else
        {
            // ������ record variable�̶� ������ �� �ִ�.
            IDE_DASSERT( sIntoVars != NULL );
            sCurrIntoVar = sIntoVars;

            sMtcColumn = QTC_STMT_COLUMN( aStatement, sCurrIntoVar );

            /* BUG-41707 */
            if ( alsRefCur == ID_FALSE )
            {
                if ( ( sMtcColumn->module->id == MTD_ROWTYPE_ID ) ||
                     ( sMtcColumn->module->id == MTD_RECORDTYPE_ID ) )
                {
                    sQtcModule = (qtcModule*)sMtcColumn->module;

                    if( ( sQtcModule->typeInfo->columnCount != aTargetCount ) &&
                        ( aIsExecImm == ID_FALSE ) )
                    {
                        sqlInfo.setSourceInfo( aStatement,
                                               &sPos );
                        IDE_RAISE(ERR_MISMATCH_INTO_COUNT);
                    }
                    else
                    {
                        // Nothing to do.
                    }
                }
                else
                {
                    // Nothing to do.
                }

                if ( sMtcColumn->module->id == MTD_ASSOCIATIVE_ARRAY_ID )
                {
                    sQtcModule = (qtcModule*)sMtcColumn->module;

                    sRowColumn = sQtcModule->typeInfo->columns->next;
                    sRowModule = (qtcModule*)sRowColumn->basicInfo->module;

                    if ( ( sRowModule->typeInfo->columnCount != aTargetCount ) && 
                         ( aIsExecImm == ID_FALSE ) )
                    {
                        sqlInfo.setSourceInfo( aStatement,
                                               &sPos );
                        IDE_RAISE(ERR_MISMATCH_INTO_COUNT);
                    }
                    else
                    {
                        // Nothing to do.
                    }
                }
                else
                {
                    // Nothing to do.
                }
            }
            else
            {
                IDE_DASSERT( aIntoVarCount != NULL );
                sQtcModule = (qtcModule*)sMtcColumn->module;
                *aIntoVarCount = sQtcModule->typeInfo->columnCount;
            } /* alsRefCur == ID_TRUE */
        } /* sIntoVarCount == 1 */
    } /* sExistsRecordVar == ID_TRUE */
    else
    {
        /* BUG-41707 */
        if ( alsRefCur == ID_TRUE )
        {
            IDE_DASSERT( aIntoVarCount != NULL );
            *aIntoVarCount = sIntoVarCount;
        }
        else
        {
            if ( ( sIntoVarCount != aTargetCount ) &&
                 ( aIsExecImm == ID_FALSE ) )
            {
                sqlInfo.setSourceInfo( aStatement,
                                       &sPos );
                IDE_RAISE(ERR_MISMATCH_INTO_COUNT);
            }
            else
            {
                // Nothing to do.
            }
        } /* aIsRefCur == ID_FALSE */
    } /* sExistsRecordVar == ID_FALSE */

    *aExistsRecordVar = sExistsRecordVar;

    return IDE_SUCCESS;

  
    IDE_EXCEPTION(ERR_NOT_FOUND_VAR);
    {
        (void)sqlInfo.init(aStatement->qmeMem);
        IDE_SET(
            ideSetErrorCode(qpERR_ABORT_QSV_NOT_EXIST_VARIABLE_NAME_SQLTEXT,
                            sqlInfo.getErrMessage() ));
        (void)sqlInfo.fini();
    }
    IDE_EXCEPTION(ERR_INOUT_TYPE_MISMATCH);
    {
        (void)sqlInfo.init(aStatement->qmeMem);
        IDE_SET(
            ideSetErrorCode
            (   qpERR_ABORT_QSV_PROC_SELECT_INTO_NO_READONLY_VAR_SQLTEXT,
                sqlInfo.getErrMessage() ));
        (void)sqlInfo.fini();
    }
    IDE_EXCEPTION(ERR_NOT_ASSOCIATIVE);
    {
        (void)sqlInfo.init(aStatement->qmeMem);
        IDE_SET(
            ideSetErrorCode(qpERR_ABORT_QSV_ONLY_ASSOCIATIVE,
                            sqlInfo.getErrMessage() ));
        (void)sqlInfo.fini();
    }
    IDE_EXCEPTION(ERR_NOT_BULK_ASSOCIATIVE);
    {
        (void)sqlInfo.init(aStatement->qmeMem);
        IDE_SET(
            ideSetErrorCode(qpERR_ABORT_QSV_BULK_ASSOCIATIVE,
                            sqlInfo.getErrMessage() ));
        (void)sqlInfo.fini();
    }
    IDE_EXCEPTION(ERR_RECORD_COUNT);
    {
        (void)sqlInfo.init(aStatement->qmeMem);
        IDE_SET(
            ideSetErrorCode(qpERR_ABORT_QSV_RECORD_WRONG,
                            sqlInfo.getErrMessage() ));
        (void)sqlInfo.fini();
    }
    IDE_EXCEPTION(ERR_MISMATCH_INTO_COUNT);
    {
        (void)sqlInfo.init(aStatement->qmeMem);
        IDE_SET(ideSetErrorCode(qpERR_ABORT_QSV_MISMATCHED_INTO_LIST_SQLTEXT,
                                sqlInfo.getErrMessage() ));
        (void)sqlInfo.fini();
    }
    IDE_EXCEPTION( ERR_UNEXPECTED_MODULE_ERROR )
    {
        IDE_SET( ideSetErrorCode( qpERR_ABORT_QMC_UNEXPECTED_ERROR,
                                  "qsvProcStmts::validateIntoClauseInternal",
                                  "The module is unexpected" ) );
    }
    IDE_EXCEPTION_END;

    return IDE_FAILURE;
}

/* BUG-41240 EXCEPTION_INIT Pragma */
IDE_RC qsvProcStmts::validatePragmaExcepInit( qcStatement           * aStatement,
                                              qsVariableItems       * aVariableItems,
                                              qsPragmaExceptionInit * aPragmaExcepInit )
{
    qsVariableItems  * sVariable        = NULL;
    idBool             sIsFound         = ID_FALSE;
    UInt               sServerErrorCode = 0;
    SChar              sExceptionName[QC_MAX_OBJECT_NAME_LEN + 1]; 
    qcuSqlSourceInfo   sqlInfo;

    IDE_DASSERT( aStatement != NULL );
    IDE_DASSERT( aVariableItems != NULL );
    IDE_DASSERT( aPragmaExcepInit != NULL );

    for ( sVariable = aVariableItems;
          (sVariable != aStatement->spvEnv->currDeclItem) && ( sVariable != NULL );
          sVariable = sVariable->next )
    {
        if ( (QC_IS_NAME_MATCHED( aPragmaExcepInit->exceptionName,
                                  sVariable->name )
              == ID_TRUE) &&
             (sVariable->itemType == QS_EXCEPTION) )
        {
            /* BUG-41240 EXCEPTION_INIT Pragma 
               aPragmaExcepInit->userErrorCode�� ������ sServerErrorCode�� ã�´�. */ 
            // BUGBUG manual�� �ִ� �����ڵ带 ������ �������� ����ϴ� �����ڵ带 ã�� �� ����.
            // sServerErrorCode = ideGetServerErrorCode( aPragmaExcepInit->userErrorCode );

            if( sServerErrorCode == ID_UINT_MAX )
            {
                IDE_RAISE( ERR_INVALID_ERROR_CODE );
            }
            else
            {
                ((qsExceptionDefs *)sVariable)->userErrorCode = aPragmaExcepInit->userErrorCode;
                ((qsExceptionDefs *)sVariable)->errorCode     = sServerErrorCode;
                sIsFound = ID_TRUE;
                break;
            }
        }
        else
        {
            // Nothing to do.
        }
    } 

    IDE_TEST_RAISE( sIsFound == ID_FALSE, ERR_NOT_EXIST_EXCEPTION_IN_THE_SAME_BLOCK );

    return IDE_SUCCESS;
    
    IDE_EXCEPTION( ERR_INVALID_ERROR_CODE )
    {
        sqlInfo.setSourceInfo( aStatement,
                               & aPragmaExcepInit->exceptionName );

        (void)sqlInfo.init(aStatement->qmeMem);
        IDE_SET(
            ideSetErrorCode( qpERR_ABORT_QSV_INVALID_EXCEPTION_ERROR_CODE,
                             aPragmaExcepInit->userErrorCode,
                             sqlInfo.getErrMessage() ));
        (void)sqlInfo.fini();
    }
    IDE_EXCEPTION( ERR_NOT_EXIST_EXCEPTION_IN_THE_SAME_BLOCK );
    {
        QC_STR_COPY( sExceptionName,
                     aPragmaExcepInit->exceptionName );

        sqlInfo.setSourceInfo( aStatement,
                               & aPragmaExcepInit->exceptionName );

        (void)sqlInfo.init(aStatement->qmeMem);
        IDE_SET(
            ideSetErrorCode( qpERR_ABORT_QSV_EXCEPTION_IS_NOT_DECLARED_IN_SAME_BLOCK,
                             sExceptionName,
                             sqlInfo.getErrMessage() ));
        (void)sqlInfo.fini();
    }
    IDE_EXCEPTION_END;

    return IDE_FAILURE;
}

idBool qsvProcStmts::checkDupErrorCode( qsExceptions * aTarget1,
                                        qsExceptions * aTarget2 )
{
    idBool sResult = ID_FALSE;

    if ( (aTarget1->userErrorCode == 0) && (aTarget2->userErrorCode == 0) )
    {
        // Nothing to do.
    }
    else
    {
        // aTarget1�� aTarget2 �� �ϳ��� error code�� 0�� ���� ����.
        if ( aTarget1->userErrorCode == aTarget2->userErrorCode )
        {
            sResult = ID_TRUE;
        }
        else
        {
            // Nothing to do.
        }
    }

    return sResult;
}