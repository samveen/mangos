 (1328,0,0,0,0,0x0000000000000000,0xC4000001,0),
 (12966,0,0,0,0,0x0000000000000000,0xC4000001,0),
 (12967,0,0,0,0,0x0000000000000000,0xC4000001,0),
 (12968,0,0,0,0,0x0000000000000000,0xC4000001,0),
 (12969,0,0,0,0,0x0000000000000000,0xC4000001,0),
 (12970,0,0,0,0,0x0000000000000000,0xC4000001,0),
 (13877,0,0,0,0,0x0000000000000000,0xC4000001,0),
 (16257,0,0,0,0,0x0000000000000000,0xC4000001,0),
 (16277,0,0,0,0,0x0000000000000000,0xC4000001,0),
 (16278,0,0,0,0,0x0000000000000000,0xC4000001,0),
 (16279,0,0,0,0,0x0000000000000000,0xC4000001,0),
 (16280,0,0,0,0,0x0000000000000000,0xC4000001,0),
 (17687,0,0,0,0,0x0000000000000000,0xC4000001,0),
 (29448,0,0,0,0,0x0000000000000000,0xC4000001,0),
 (35205,0,0,0,0,0x0000000000000000,0xC4000001,0),
 (33735,0,0,0,0,0x0000000000000000,0xC4000001,0),
 (40353,0,0,0,0,0x0000000000000000,0xC4000001,0),


DELETE FROM `spell_proc_event` WHERE `entry` IN (13877,33735);
INSERT INTO `spell_proc_event` VALUES
 (13877,0,0,0,0,0x0000000000000000,0xC4000001,0),
 (33735,0,0,0,0,0x0000000000000000,0xC4000001,0);

DELETE FROM `spell_proc_event` WHERE `entry` IN (12966,12967,12968,12969,12970,16257,16277,16278,16279,16280,17687);
INSERT INTO `spell_proc_event` VALUES
 (12966,0,0,0,0,0x0000000000000000,0xC4000001,0),
 (12967,0,0,0,0,0x0000000000000000,0xC4000001,0),
 (12968,0,0,0,0,0x0000000000000000,0xC4000001,0),
 (12969,0,0,0,0,0x0000000000000000,0xC4000001,0),
 (12970,0,0,0,0,0x0000000000000000,0xC4000001,0),
 (16257,0,0,0,0,0x0000000000000000,0xC4000001,0),
 (16277,0,0,0,0,0x0000000000000000,0xC4000001,0),
 (16278,0,0,0,0,0x0000000000000000,0xC4000001,0),
 (16279,0,0,0,0,0x0000000000000000,0xC4000001,0),
 (16280,0,0,0,0,0x0000000000000000,0xC4000001,0),
 (17687,0,0,0,0,0x0000000000000000,0xC4000001,0);

DELETE FROM `spell_proc_event` WHERE `entry` IN (40353);
INSERT INTO `spell_proc_event` VALUES
 (40353,0,0,0,0,0x0000000000000000,0xC4000001,0);

DELETE FROM `spell_proc_event` WHERE `entry` IN (29448,35205);
INSERT INTO `spell_proc_event` VALUES
 (29448,0,0,0,0,0x0000000000000000,0xC4000001,0),
 (35205,0,0,0,0,0x0000000000000000,0xC4000001,0);

DELETE FROM `spell_proc_event` WHERE `entry` IN (1328);
INSERT INTO `spell_proc_event` VALUES
 (1328,0,0,0,0,0x0000000000000000,0xC4000001,0);



