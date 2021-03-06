BEGIN TRANSACTION;

CREATE TEMPORARY TABLE `products_backup` (
    `id`	INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT,
    `itemnum`	text NOT NULL,
    `barcode`	text NOT NULL,
    `name`	text NOT NULL,
    `sold`	double NOT NULL DEFAULT 0,
    `net`	double NOT NULL,
    `gross`	double NOT NULL,
    `group`	INTEGER NOT NULL DEFAULT 2,
    `visible`	tinyint(1) NOT NULL DEFAULT 1,
    `completer`	tinyint(1) NOT NULL DEFAULT 1,
    `tax`	double NOT NULL DEFAULT '20',
    `color`     text DEFAULT '#808080',
    `button`    text DEFAULT '',
    `image`     text DEFAULT '',
    `coupon`	tinyint(1) NOT NULL DEFAULT 0,
    CONSTRAINT `group` FOREIGN KEY (`group`) REFERENCES `groups` (`id`)
);
INSERT INTO products_backup SELECT `id`,`itemnum`,`barcode`,`name`,`sold`,`net`,`gross`,`group`,`visible`,`completer`,`tax`,`color`,`button`,`image`, `coupon` FROM `products`;
DROP TABLE `products`;

CREATE TABLE `products` (
    `id`	INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT,
    `itemnum`	text NOT NULL,
    `barcode`	text NOT NULL,
    `name`	text NOT NULL,
    `sold`	double NOT NULL DEFAULT 0,
    `net`	double NOT NULL,
    `gross`	double NOT NULL,
    `group`	INTEGER NOT NULL DEFAULT 2,
    `visible`	tinyint(1) NOT NULL DEFAULT 1,
    `completer`	tinyint(1) NOT NULL DEFAULT 1,
    `tax`	double NOT NULL DEFAULT '20',
    `color`     text DEFAULT '#808080',
    `button`    text DEFAULT '',
    `image`     text DEFAULT '',
    `coupon`	tinyint(1) NOT NULL DEFAULT 0,
    `stock`	double NOT NULL DEFAULT 0,
    `minstock`	double NOT NULL DEFAULT 0,
    CONSTRAINT `group` FOREIGN KEY (`group`) REFERENCES `groups` (`id`)
);
INSERT INTO products SELECT `id`,`itemnum`,`barcode`,`name`,`sold`,`net`,`gross`,`group`,`visible`,`completer`,`tax`,`color`,`button`,`image`, `coupon`, 0,0 FROM `products_backup`;
DROP TABLE `products_backup`;

COMMIT;
