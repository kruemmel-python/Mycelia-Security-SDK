<?php
session_start();
function require_env($name) {
    $value = getenv($name);
    if ($value === false || $value === '') {
        http_response_code(500);
        die("Fehlende Umgebungsvariable: $name");
    }
    return $value;
}

// Verbindung mit Umgebungsvariablen
$db_host = require_env('MYCELIA_DB_HOST');
$db_name = require_env('MYCELIA_DB_NAME');
$db_user = require_env('MYCELIA_DB_USER');
$db_pass = require_env('MYCELIA_DB_PASSWORD');

try {
    $dsn = "mysql:host={$db_host};dbname={$db_name}";
    $pdo = new PDO($dsn, $db_user, $db_pass);
} catch (PDOException $e) {
    die("Konnte nicht zur DB verbinden. Bitte erst setup_db.php ausführen!");
}

require 'api.php';

$message = "";

// --- REGISTRIERUNG ---
if (isset($_POST['register'])) {
    $user = $_POST['user'];
    $pass = password_hash($_POST['pass'], PASSWORD_DEFAULT);
    
    $personal_data = json_encode([
        'vorname' => $_POST['vorname'],
        'nachname' => $_POST['nachname'],
        'strasse' => $_POST['strasse'],
        'hnr' => $_POST['hnr'],
        'plz' => $_POST['plz'],
        'ort' => $_POST['ort'],
        'email' => $_POST['email']
    ]);

    // An GPU senden
    $crypto = call_mycelia('encrypt', $personal_data);
    
    if ($crypto['status'] == 'ok') {
        $stmt = $pdo->prepare("INSERT INTO users (username, password_hash, mycelia_seed, encrypted_blob) VALUES (?, ?, ?, ?)");
        try {
            $stmt->execute([$user, $pass, $crypto['seed'], $crypto['blob']]);
            $message = "Registrierung erfolgreich (GPU-Secured). Bitte einloggen.";
        } catch (PDOException $e) {
            $message = "Username vergeben.";
        }
    }
}

// --- LOGIN ---
if (isset($_POST['login'])) {
    $user = $_POST['user'];
    $pass = $_POST['pass'];
    
    $stmt = $pdo->prepare("SELECT * FROM users WHERE username = ?");
    $stmt->execute([$user]);
    $row = $stmt->fetch();
    
    if ($row && password_verify($pass, $row['password_hash'])) {
        $_SESSION['user_id'] = $row['id'];
        header("Location: profile.php");
        exit;
    } else {
        $message = "Falsche Daten.";
    }
}
?>

<!DOCTYPE html>
<html lang="de">
<head>
    <meta charset="UTF-8">
    <title>Mycelia Secure Login</title>
    <style>
        body { background: #121212; color: #00ff99; font-family: monospace; text-align: center; margin-top: 50px; }
        input { background: #333; border: 1px solid #555; color: white; padding: 5px; margin: 5px; width: 90%; box-sizing:border-box;}
        button { background: #00ff99; border: none; padding: 10px 20px; font-weight: bold; cursor: pointer; width: 100%; margin-top:10px;}
        .container { display: flex; justify-content: center; gap: 20px; flex-wrap: wrap; }
        .box { border: 1px solid #333; background: #1e1e1e; padding: 20px; width: 300px; text-align: left; }
        h2 { border-bottom: 1px solid #333; padding-bottom: 10px; margin-top: 0; }
    </style>
</head>
<body>
    <h1 style="border: 2px solid #00ff99; display:inline-block; padding: 10px;">MYCELIA SECURITY GATE</h1>
    <p style="color:#ff5555; font-weight:bold;"><?= $message ?></p>

    <div class="container">
        <div class="box">
            <h2>Login</h2>
            <form method="post">
                <label>Username</label><br>
                <input type="text" name="user" required><br>
                <label>Passwort</label><br>
                <input type="password" name="pass" required><br>
                <button type="submit" name="login">ZUGRIFF ERBITTEN</button>
            </form>
        </div>

        <div class="box">
            <h2>Registrierung</h2>
            <form method="post">
                <label>Username</label><br>
                <input type="text" name="user" required><br>
                <label>Passwort</label><br>
                <input type="password" name="pass" required><br>
                <hr style="border-color:#333">
                <input type="text" name="vorname" placeholder="Vorname"><br>
                <input type="text" name="nachname" placeholder="Nachname"><br>
                <input type="text" name="strasse" placeholder="Straße"><br>
                <input type="text" name="hnr" placeholder="Nr." style="width: 25%"><input type="text" name="plz" placeholder="PLZ" style="width: 60%"><br>
                <input type="text" name="ort" placeholder="Ort"><br>
                <input type="email" name="email" placeholder="E-Mail"><br>
                <button type="submit" name="register">VERSCHLÜSSELT SPEICHERN</button>
            </form>
        </div>
    </div>
</body>
</html>
