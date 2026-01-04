<?php
session_start();
if (!isset($_SESSION['user_id'])) { header("Location: index.php"); exit; }

if (!file_exists('config.php')) {
    die("Fehler: config.php fehlt!");
}
require_once 'config.php';

// Verbindung mit Passwort 1234
$dsn = "mysql:host=" . DB_HOST . ";dbname=" . DB_NAME;
$pdo = new PDO($dsn, DB_USER, DB_PASS);
require 'api.php';

$uid = $_SESSION['user_id'];
$msg = "";

// --- UPDATE ---
if (isset($_POST['update'])) {
    $personal_data = json_encode([
        'vorname' => $_POST['vorname'],
        'nachname' => $_POST['nachname'],
        'strasse' => $_POST['strasse'],
        'hnr' => $_POST['hnr'],
        'plz' => $_POST['plz'],
        'ort' => $_POST['ort'],
        'email' => $_POST['email']
    ]);
    
    $crypto = call_mycelia('encrypt', $personal_data);
    
    if ($crypto['status'] == 'ok') {
        $stmt = $pdo->prepare("UPDATE users SET mycelia_seed = ?, encrypted_blob = ? WHERE id = ?");
        $stmt->execute([$crypto['seed'], $crypto['blob'], $uid]);
        $msg = "Profil sicher aktualisiert (Neuer Seed generiert).";
    }
}

// --- DATEN LADEN ---
$stmt = $pdo->prepare("SELECT mycelia_seed, encrypted_blob, username FROM users WHERE id = ?");
$stmt->execute([$uid]);
$row = $stmt->fetch();

// GPU Entschlüsselung
$decrypted_response = call_mycelia('decrypt', ['seed' => $row['mycelia_seed'], 'blob' => $row['encrypted_blob']]);

$data = [];
if ($decrypted_response['status'] == 'ok') {
    $data = json_decode($decrypted_response['data'], true);
} else {
    die("<h1 style='color:red'>INTEGRITY ERROR</h1>GPU konnte Daten nicht rekonstruieren.");
}
?>

<!DOCTYPE html>
<html lang="de">
<head>
    <meta charset="UTF-8">
    <title>Secure Profile</title>
    <style>
        body { background: #121212; color: #e0e0e0; font-family: monospace; padding: 50px; }
        .box { background: #1e1e1e; border: 1px solid #333; padding: 20px; width: 500px; margin: 0 auto; }
        input { background: #222; border: 1px solid #444; color: white; padding: 8px; width: 100%; box-sizing: border-box; margin-bottom: 10px; }
        button { background: #00ff99; color: black; border: none; padding: 10px; font-weight: bold; cursor: pointer; width: 100%; }
        .raw { background: #000; color: #555; padding: 10px; border: 1px dashed #333; margin-bottom: 20px; font-size: 10px; word-break: break-all;}
        h1 { color: #00ff99; text-align: center; }
        .label { color: #888; font-size: 0.8em; }
    </style>
</head>
<body>
    <h1>Willkommen, <?= htmlspecialchars($row['username']) ?></h1>
    
    <div class="box">
        <?php if($msg): ?><p style="color:#00ff99; text-align:center;"><?= $msg ?></p><?php endif; ?>

        <h3>Was die Datenbank sieht (Encrypted Blob):</h3>
        <div class="raw">
            SEED: <?= $row['mycelia_seed'] ?><br><br>
            DATA: <?= substr($row['encrypted_blob'], 0, 150) ?>... [REDACTED]
        </div>

        <h3>Was du siehst (GPU Decrypted):</h3>
        <form method="post">
            <span class="label">Vorname</span><input type="text" name="vorname" value="<?= $data['vorname'] ?? '' ?>">
            <span class="label">Nachname</span><input type="text" name="nachname" value="<?= $data['nachname'] ?? '' ?>">
            <span class="label">Straße</span><input type="text" name="strasse" value="<?= $data['strasse'] ?? '' ?>">
            <div style="display:flex; gap:10px;">
                <div style="flex:1"><span class="label">Nr.</span><input type="text" name="hnr" value="<?= $data['hnr'] ?? '' ?>"></div>
                <div style="flex:2"><span class="label">PLZ</span><input type="text" name="plz" value="<?= $data['plz'] ?? '' ?>"></div>
            </div>
            <span class="label">Ort</span><input type="text" name="ort" value="<?= $data['ort'] ?? '' ?>">
            <span class="label">Email</span><input type="email" name="email" value="<?= $data['email'] ?? '' ?>">
            
            <button type="submit" name="update">Update Secure Profile</button>
        </form>
        <br>
        <center><a href="index.php" style="color:#666;">Abmelden</a></center>
    </div>
</body>
</html>
