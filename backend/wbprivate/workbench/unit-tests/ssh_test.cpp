/*
 * Copyright (c) 2017, 2018, Oracle and/or its affiliates. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2.0,
 * as published by the Free Software Foundation.
 *
 * This program is also distributed with certain software (including
 * but not limited to OpenSSL) that is licensed under separate terms, as
 * designated in a particular file or component or in included license
 * documentation.  The authors of MySQL hereby grant you an additional
 * permission to link the program and your derivative works with the
 * separately licensed software that they have included with MySQL.
 * This program is distributed in the hope that it will be useful,  but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
 * the GNU General Public License, version 2.0, for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 */

// High-level testing for Workbench
// This tests WBContext, which will test the integration of all components.

#include "base/util_functions.h"
#include "base/file_utilities.h"

#include "wb_helpers.h"
#include "workbench/SSHCommon.h"
#include "workbench/SSHTunnelManager.h"
#include "workbench/SSHSessionWrapper.h"
#include "workbench/SSHSftp.h"

BEGIN_TEST_DATA_CLASS(ssh_test)
protected:
WBTester *_tester;
db_mgmt_ConnectionRef connectionProperties;

TEST_DATA_CONSTRUCTOR(ssh_test) {
  _tester = new WBTester;
  // Init datatypes and RDBMS.
  populate_grt(*_tester);
}

END_TEST_DATA_CLASS;

TEST_MODULE(ssh_test, "ssh testing");

TEST_FUNCTION(2) {

  ssh::SSHConnectionConfig config;
  config.localhost = "127.0.0.1";
  config.remoteSSHhost = test_params->getSSHHostName();
  config.remoteSSHport = test_params->getSSHPort();
  config.connectTimeout = 10;
  config.optionsDir = test_params->getSSHOptionsDir();

  auto file = base::makeTmpFile("/tmp/known_hosts");
  std::string knownHosts = file.getPath();
  file.dispose();

  config.knownHostsFile = knownHosts;

  ssh::SSHConnectionCredentials credentials;
  credentials.username = test_params->getSSHUserName();
  credentials.password = test_params->getSSHPassword();
  credentials.auth = ssh::SSHAuthtype::PASSWORD;

  auto session = ssh::SSHSession::createSession();
  auto retVal = session->connect(config, credentials);
  ensure_true("fingerprint unknown", std::get<0>(retVal) == ssh::SSHReturnType::FINGERPRINT_UNKNOWN);
  session->disconnect();
  // have to use tmp val to make clang happy
  std::string tmp = std::get<1>(retVal);
  config.fingerprint = tmp;
  retVal = session->connect(config, credentials);
  ensure_true("connection established", std::get<0>(retVal) == ssh::SSHReturnType::CONNECTED);
  ensure_true("connection status, is connected", session->isConnected());
  session->disconnect();
  ensure_false("connection status, is disconnected", session->isConnected());

  if (base::file_exists(knownHosts))
    base::remove(knownHosts);
}

TEST_FUNCTION(3) {
  ssh::SSHConnectionConfig config;
  config.localhost = "127.0.0.1";
  config.remoteSSHhost = test_params->getSSHHostName();
  config.remoteSSHport = test_params->getSSHPort();
  config.connectTimeout = 10;
  config.strictHostKeyCheck = false;
  ssh::SSHConnectionCredentials credentials;
  credentials.username = test_params->getSSHUserName();
  credentials.password = test_params->getSSHPassword();
  credentials.auth = ssh::SSHAuthtype::PASSWORD;

  auto session = ssh::SSHSession::createSession();

  auto retVal = session->connect(config, credentials);
  ensure_true("connection established", std::get<0>(retVal) == ssh::SSHReturnType::CONNECTED);
  ensure_true("connection status, is connected", session->isConnected());

  ssh::SSHSftp sftp(session, 65535);
  std::cout << "Current dir is: " << sftp.pwd() << std::endl;


  std::string randomDir = "test_tut_" + randomString();
  try {
    sftp.mkdir(randomDir);
  } catch (ssh::SSHSftpException &exc) {
    fail("Unable to create remote directory");
  }

  try {
    auto info = sftp.stat(randomDir);
  } catch (ssh::SSHSftpException &exc) {
    fail("Unable to stat remote directory");
  }

  try {
    sftp.mkdir(randomDir);
    fail("Directory was created but it shouldn't: test_tut");
  } catch (ssh::SSHSftpException &exc) {
    // pass
  }

  try {
      sftp.rmdir(randomDir);
  } catch (ssh::SSHSftpException &exc) {
    fail("Unable to remove directory");
  }

  std::string sampleText = "Lorem ipsum dolor sit amet, consectetur adipiscing elit. Praesent scelerisque quam ac.";
  std::string testFile = "tut_ssh_test_" + randomString();
  try {
    sftp.setContent(testFile, sampleText);
  } catch (ssh::SSHSftpException &exc) {
    fail("Unable to create file");
  }

  try {
    std::string content = sftp.getContent(testFile);
    ensure_equals("File content missmatch", content, sampleText);
  } catch (ssh::SSHSftpException &exc) {
    fail("Unable to get contents of file: " + testFile);
  }

  auto file = base::makeTmpFile("tremporary_download");
  auto tmpFilePath = file.getPath();
  file.dispose();
  try {
    sftp.get(testFile, tmpFilePath);
    std::string fContents = base::getTextFileContent(tmpFilePath);
    ensure_true("Get file content failed", base::same_string(fContents, sampleText, true));
  } catch (ssh::SSHSftpException &exc) {
    std::string msg = "Unable to get contents of file: " + testFile + " error:";
    msg.append(exc.what());
    fail(msg);
  }
  base::remove(tmpFilePath);
  auto currentDir = sftp.pwd();
  ensure_true("Can't use /home directory, check ftp configuration", sftp.cd("/home"));
  ensure_equals("Invalid current directory information", "/home", sftp.pwd());
  ensure_true("Can't change to parent dir, check ftp configuration", sftp.cd(".."));
  ensure_true("Can't change to /home, check ftp configuration", sftp.cd("home"));
  ensure_true("Unable to switch to initial directory", sftp.cd(currentDir));

  ensure_true("Existing directory /this_is_invalid", sftp.cd("/this_is_invalid") == -1);
  ensure_true("Dir /etc/ssl/private should be restricted", sftp.cd("/etc/ssl/private") == -2);
  ensure_equals("Invalid current directory information", currentDir, sftp.pwd());


}

TEST_FUNCTION(4) {

  ssh::SSHConnectionConfig config;
  config.localhost = "127.0.0.1";
  config.remoteSSHhost = test_params->getSSHHostName();
  config.remoteSSHport = test_params->getSSHPort();
  config.connectTimeout = 10;
  config.optionsDir = test_params->getSSHOptionsDir();
  config.strictHostKeyCheck = false;
  ssh::SSHConnectionCredentials credentials;
  credentials.username = test_params->getSSHUserName();
  credentials.password = test_params->getSSHPassword();
  credentials.auth = ssh::SSHAuthtype::PASSWORD;

  auto session = ssh::SSHSession::createSession();

  auto retVal = session->connect(config, credentials);
  ensure_true("connection established", std::get<0>(retVal) == ssh::SSHReturnType::CONNECTED);
  ensure_true("connection status, is connected", session->isConnected());

  std::string output = session->execCmd("echo 'This is tut test'");
  ensure_equals("command run succesfully ", output, "This is tut test\n");


  try {
    std::string output = session->execCmdSudo("sudo -k -S -p EnterPasswordHere echo 'This is tut test'", "invalidpw", "EnterPasswordHere");
    fail("Missing exception");
  } catch (ssh::SSHAuthException &exc) {
    //pass it's fine
  }

  session->disconnect();
  ensure_false("connection status, is disconnected", session->isConnected());
}

TEST_FUNCTION(5) {
  ssh::SSHConnectionConfig config;
  config.localhost = "127.0.0.1";
  config.remotehost = "127.0.0.1";
  config.remoteport = test_params->getSSHDbPort();
  config.remoteSSHhost = test_params->getSSHHostName();
  config.remoteSSHport = test_params->getSSHPort();
  config.connectTimeout = 10;
  config.optionsDir = test_params->getSSHOptionsDir();
  config.strictHostKeyCheck = false;
  ssh::SSHConnectionCredentials credentials;
  credentials.username = test_params->getSSHUserName();
  credentials.password = test_params->getSSHPassword();
  credentials.auth = ssh::SSHAuthtype::PASSWORD;

  auto manager = std::unique_ptr<ssh::SSHTunnelManager>(new ssh::SSHTunnelManager());

  //this should start new worker thread
  manager->start();

  auto session = ssh::SSHSession::createSession();

  auto retVal = session->connect(config, credentials);
  ensure_true("connection established", std::get<0>(retVal) == ssh::SSHReturnType::CONNECTED);
  ensure_true("connection status, is connected", session->isConnected());

  std::this_thread::sleep_for(std::chrono::seconds(1));
  ensure_true("Tunnel Manager isn't running", manager->isRunning());

  try {
    retVal = manager->createTunnel(session);
  } catch (ssh::SSHTunnelException &se) {
    fail(std::string("Unable to create tunnel: ").append(se.what()));
  }

  uint16_t port = std::get<1>(retVal);

  sql::DriverManager *dm = sql::DriverManager::getDriverManager();
  ensure("dm is NULL", dm != NULL);

  connectionProperties = db_mgmt_ConnectionRef(grt::Initialized);
  grt::DictRef conn_params(true);
  conn_params.set("hostName", grt::StringRef(config.localhost));
  conn_params.set("port", grt::IntegerRef(port));
  conn_params.set("userName", grt::StringRef(test_params->getSSHDbUser()));
  conn_params.set("password", grt::StringRef(test_params->getSSHDbPassword()));
  grt::replace_contents(connectionProperties->parameterValues(), conn_params);
  db_mgmt_DriverRef driverProperties(grt::Initialized);
  driverProperties->driverLibraryName(grt::StringRef("mysqlcppconn"));
  connectionProperties->driver(driverProperties);

  try {
    std::vector<sql::ConnectionWrapper> wrapperList;
    
    for (int i =0; i < 4; i++) {
      sql::ConnectionWrapper wrapper = dm->getConnection(connectionProperties);
      ensure("conn is NULL", wrapper.get() != NULL);
      wrapperList.push_back(wrapper);
    }
    
    for(auto &it: wrapperList) {
      sql::Connection *conn = it.get();
      std::auto_ptr<sql::Statement> stmt(conn->createStatement());
      ensure("stmt1 is NULL", stmt.get() != NULL);
      
      std::auto_ptr<sql::ResultSet> rset(stmt->executeQuery("SELECT CONNECTION_ID()"));
      ensure("res1 is NULL", rset.get() != NULL);
      
      ensure("res1 is empty", rset->next() != false);
    }
  } catch (std::exception &exc)
  {
    manager->setStop();
    manager->pokeWakeupSocket();
    fail(std::string("Unable to make tunnel connection. ").append(exc.what()));
  }

  manager->setStop();
  manager->pokeWakeupSocket();
}

// Due to the tut nature, this must be executed as a last test always,
// we can't have this inside of the d-tor.
TEST_FUNCTION(99) {
  delete _tester;
}

END_TESTS
