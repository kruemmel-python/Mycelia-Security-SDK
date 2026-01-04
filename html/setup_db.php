<?php
if (!file_exists('config.php')) {
    die("Fehler: config.php fehlt!");
}
require_once 'config.php';

$host = DB_HOST;
$user = DB_USER;
$pass = DB_PASS;

try {
    // 1. Verbindung ohne DB, um sie zu erstellen
    $pdo = new PDO("mysql:host=$host", $user, $pass);
    $pdo->setAttribute(PDO::ATTR_ERRMODE, PDO::ERRMODE_EXCEPTION);

    echo "Verbindung zu MySQL erfolgreich.<br>";

    // 2. Datenbank erstellen
    $pdo->exec("CREATE DATABASE IF NOT EXISTS " . DB_NAME);
    echo "Datenbank 'mycelia_secure_db' geprüft/erstellt.<br>";

    // 3. Tabelle erstellen
    $pdo->exec("USE " . DB_NAME);
    $sql = "CREATE TABLE IF NOT EXISTS users (
        id INT AUTO_INCREMENT PRIMARY KEY,
        username VARCHAR(50) NOT NULL UNIQUE,
        password_hash VARCHAR(255) NOT NULL,
        mycelia_seed BIGINT NOT NULL,
        encrypted_blob LONGTEXT NOT NULL,
        created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
    )";
    $pdo->exec($sql);
    echo "Tabelle 'users' geprüft/erstellt.<br>";
    echo "<h3 style='color:green'>Fertig! Du kannst jetzt index.php öffnen.</h3>";

} catch (PDOException $e) {
    die("DB FEHLER: " . $e->getMessage());
}
?>
