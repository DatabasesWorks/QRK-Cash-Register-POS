SET FOREIGN_KEY_CHECKS=0;
SET SQL_MODE = "NO_AUTO_VALUE_ON_ZERO";
START TRANSACTION;

DELETE FROM globals WHERE name = 'turnovercounter';
DELETE FROM globals WHERE name = 'lastUsedCertificate';

SET FOREIGN_KEY_CHECKS=1;
COMMIT;
