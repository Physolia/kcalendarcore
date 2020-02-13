/*
  This file is part of the kcalcore library.

  SPDX-FileCopyrightText: 2006, 2008 Allen Winter <winter@kde.org>

  SPDX-License-Identifier: LGPL-2.0-or-later
*/

#ifndef TESTTODO_H
#define TESTTODO_H

#include <QObject>

class TodoTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase();

    void testValidity();
    void testCompare();
    void testClone();
    void testCopyIncidence();
    void testAssign();
    void testSetCompleted();
    void testStatus();
    void testSerializer_data();
    void testSerializer();
    void testRoles();
};

#endif
