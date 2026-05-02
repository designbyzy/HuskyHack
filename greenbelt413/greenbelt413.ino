 /*                                                                                                                                                                      
   * STAR CHOMPER — Pac-Man style star collector                                                                                                                          
   * For Waveshare ESP32-S3-Touch-AMOLED-2.06                                                                                                                             
   *                                                                                                                                                                      
   * Libraries needed (Arduino Library Manager):                                                                                                                          
   *   - "GFX Library for Arduino" by Moon On Our Nation  (search: Arduino_GFX)                                                                                           
   *                                                                                                                                                                      
   * Board settings:                                                                                                                                                      
   *   Tools → Board       → ESP32S3 Dev Module                                                                                                                           
   *   Tools → PSRAM       → OPI PSRAM                                                                                                                                    
   *   Tools → Flash Size  → 8MB                                                                                                                                          
   *                                                                                                                                                                      
   * Controls: on-screen D-pad arrows (tap to move)                                                                                                                       
   */                                                                                                                                                                     
                  
  // ─── Display pins (Waveshare ESP32-S3-Touch-AMOLED-2.06) ─────────────────────                                                                                        
  #define LCD_CS    6
  #define LCD_SCLK  47                                                                                                                                                    
  #define LCD_SDIO0 18                                                                                                                                                    
  #define LCD_SDIO1 7
  #define LCD_SDIO2 48                                                                                                                                                    
  #define LCD_SDIO3 5                                                                                                                                                     
  #define LCD_RST   17
  #define LCD_W     410                                                                                                                                                   
  #define LCD_H     502

  // ─── Touch pins (FT3168) ──────────────────────────────────────────────────────                                                                                       
  #define TOUCH_SDA  4
  #define TOUCH_SCL  8           // FIX: was 5, which conflicts with LCD_SDIO3                                                                                            
  #define TOUCH_ADDR 0x38                                                                                                                                                 
  #define TOUCH_INT  21
                                                                                                                                                                          
  // ─── Game constants ───────────────────────────────────────────────────────────                                                                                       
  #define CELL        20
  #define COLS        (LCD_W / CELL)                                                                                                                                      
  #define ROWS        ((LCD_H - 200) / CELL)  // FIX: was 100 — need 200px for HUD+dpad to fit on screen
                                                                                                                                                                          
  #define MAX_STARS   40                                                                                                                                                  
  #define MAX_GHOSTS  3                                                                                                                                                   
  #define GHOST_SPEED_MS  250                                                                                                                                             
                                                                                                                                                                          
  // ─── Colors (RGB565) ──────────────────────────────────────────────────────────                                                                                       
  #define C_BG        0x0000                                                                                                                                              
  #define C_GRID      0x1082                                                                                                                                              
  #define C_STAR      0xFFE0                                                                                                                                              
  #define C_PLAYER    0xFFE0
  #define C_PLAYER_EYE 0x0000                                                                                                                                             
  #define C_GHOST_R   0xF800                                                                                                                                              
  #define C_GHOST_B   0x001F
  #define C_GHOST_P   0xF81F                                                                                                                                              
  #define C_HUD_BG    0x2104
  #define C_WHITE     0xFFFF                                                                                                                                              
  #define C_DPAD      0x4208
  #define C_DPAD_HI   0x7BEF                                                                                                                                              
  #define C_TEXT      0xFFFF                                                                                                                                              
  #define C_SCORE     0xFFE0                                                                                                                                              
  #define C_LIVES     0xF800                                                                                                                                              
  #define C_FLASH     0xFFFF

  #include <Arduino_GFX_Library.h>
  #include <Wire.h>

  // ─── Display + canvas ─────────────────────────────────────────────────────────                                                                                       
  Arduino_DataBus *bus = new Arduino_ESP32QSPI(
    LCD_CS, LCD_SCLK, LCD_SDIO0, LCD_SDIO1, LCD_SDIO2, LCD_SDIO3                                                                                                          
  );                                                                                                                                                                      
  Arduino_GFX *gfx = new Arduino_CO5300(
    bus, LCD_RST, 0, false, LCD_W, LCD_H                                                                                                                                  
  );                                                                                                                                                                      
  Arduino_Canvas *canvas = new Arduino_Canvas(LCD_W, LCD_H, gfx);
                                                                                                                                                                          
  // ─── Game world origin ────────────────────────────────────────────────────────
  #define WORLD_Y  0                                                                                                                                                      
  #define HUD_Y    (ROWS * CELL)                                                                                                                                          
  #define DPAD_Y   (HUD_Y + 30)
                                                                                                                                                                          
  // ─── Player ───────────────────────────────────────────────────────────────────
  struct Player {                                                                                                                                                         
    int   col, row;
    int   dir;                                                                                                                                                            
    int   nextDir;
    bool  mouthOpen;                                                                                                                                                      
    uint32_t lastMove;
    uint32_t moveDelay;                                                                                                                                                   
    int   score;
    int   lives;                                                                                                                                                          
    bool  dead;   
    uint32_t deathTime;                                                                                                                                                   
  };
                                                                                                                                                                          
  // ─── Ghost ────────────────────────────────────────────────────────────────────                                                                                       
  struct Ghost {
    int  col, row;                                                                                                                                                        
    int  dir;     
    uint16_t color;
    uint32_t lastMove;
    bool active;
  };

  // ─── Stars ────────────────────────────────────────────────────────────────────                                                                                       
  struct Star {
    int  col, row;                                                                                                                                                        
    bool collected;
    bool big;
  };                                                                                                                                                                      
   
  // ─── State ────────────────────────────────────────────────────────────────────                                                                                       
  Player  pac;    
  Ghost   ghosts[MAX_GHOSTS];                                                                                                                                             
  Star    stars[MAX_STARS];
  int     starsLeft;                                                                                                                                                      
  int     level;  
  bool    gameOver;
  bool    levelClear;                                                                                                                                                     
  uint32_t levelClearTime;
  uint32_t flashTimer;                                                                                                                                                    
  bool    flashState;

  // ─── D-pad button regions ─────────────────────────────────────────────────────                                                                                       
  #define DPAD_BTN_W  80
  #define DPAD_BTN_H  55         // FIX: was 60 — 3 rows of 60 = 180px which exceeds remaining screen space                                                               
  #define DPAD_CX     (LCD_W / 2)                                                                                                                                         
   
  struct DpadBtn {                                                                                                                                                        
    int x, y, w, h;
    int dir;                                                                                                                                                              
  };              

  DpadBtn dpad[4];

  // ─── Touch state ──────────────────────────────────────────────────────────────
  int  touchX = -1, touchY = -1;
  bool touched = false;                                                                                                                                                   
  bool touchPrev = false;
                                                                                                                                                                          
  // ─────────────────────────────────────────────────────────────────────────────
  // FORWARD DECLARATIONS                                                                                                                                                 
  // ─────────────────────────────────────────────────────────────────────────────
  void initGame();
  void initLevel();
  void spawnStars();
  void spawnGhosts();
  void updateGame();
  void movePac();                                                                                                                                                         
  void moveGhosts();
  void checkCollisions();                                                                                                                                                 
  void readTouch();
  void handleDpad();
  void render();                                                                                                                                                          
  void drawWorld();
  void drawStars();                                                                                                                                                       
  void drawPac(); 
  void drawGhosts();
  void drawHUD();
  void drawDpad();
  void drawGameOver();
  void drawLevelClear();                                                                                                                                                  
  void drawDeathFlash();        // FIX: was missing — caused compile error
  void drawCell(int col, int row, uint16_t color);                                                                                                                        
  void drawStar(int cx, int cy, bool big);
  void drawPacSprite(int cx, int cy, int dir, bool mouth);                                                                                                                
  void drawGhostSprite(int cx, int cy, uint16_t color);                                                                                                                   
                                                                                                                                                                          
  // ─────────────────────────────────────────────────────────────────────────────                                                                                        
  // SETUP                                                                                                                                                                
  // ─────────────────────────────────────────────────────────────────────────────
  void setup() {                                                                                                                                                          
    Serial.begin(115200);
                                                                                                                                                                          
    Wire.begin(TOUCH_SDA, TOUCH_SCL, 400000);                                                                                                                             
   
    if (!gfx->begin()) {                                                                                                                                                  
      Serial.println("GFX init failed!");
      while (1) delay(100);                                                                                                                                               
    }
    canvas->begin();                                                                                                                                                      
    canvas->fillScreen(C_BG);
    canvas->flush();                                                                                                                                                      
   
    int dpY = DPAD_Y;                                                                                                                                                     
    dpad[2] = { DPAD_CX - DPAD_BTN_W/2,       dpY,                     DPAD_BTN_W, DPAD_BTN_H, 2 }; // UP
    dpad[1] = { DPAD_CX - DPAD_BTN_W*3/2 - 4, dpY + DPAD_BTN_H,        DPAD_BTN_W, DPAD_BTN_H, 1 }; // LEFT                                                               
    dpad[0] = { DPAD_CX + DPAD_BTN_W/2 + 4,   dpY + DPAD_BTN_H,        DPAD_BTN_W, DPAD_BTN_H, 0 }; // RIGHT                                                              
    dpad[3] = { DPAD_CX - DPAD_BTN_W/2,       dpY + DPAD_BTN_H * 2,    DPAD_BTN_W, DPAD_BTN_H, 3 }; // DOWN                                                               
                                                                                                                                                                          
    canvas->fillScreen(C_BG);                                                                                                                                             
    canvas->setTextColor(C_STAR);
    canvas->setTextSize(3);                                                                                                                                               
    canvas->setCursor(70, 180);
    canvas->print("STAR");                                                                                                                                                
    canvas->setCursor(30, 220);
    canvas->print("CHOMPER");
    canvas->setTextColor(C_WHITE);
    canvas->setTextSize(1);                                                                                                                                               
    canvas->setCursor(80, 290);
    canvas->print("Tap any arrow to start");                                                                                                                              
    canvas->flush();                                                                                                                                                      
    delay(2000);
                                                                                                                                                                          
    initGame();                                                                                                                                                           
  }
                                                                                                                                                                          
  // ─────────────────────────────────────────────────────────────────────────────
  // LOOP
  // ─────────────────────────────────────────────────────────────────────────────
  void loop() {                                                                                                                                                           
    readTouch();
                                                                                                                                                                          
    if (gameOver) {
      render();
      if (touched && !touchPrev) {
        initGame();
      }                                                                                                                                                                   
      touchPrev = touched;
      return;                                                                                                                                                             
    }             

    if (levelClear) {
      render();
      if (millis() - levelClearTime > 2000) {
        level++;
        initLevel();
      }
      return;
    }

    handleDpad();
    updateGame();
    render();                                                                                                                                                             
   
    touchPrev = touched;                                                                                                                                                  
    delay(16);    
  }

  // ─────────────────────────────────────────────────────────────────────────────                                                                                        
  // GAME INIT
  // ─────────────────────────────────────────────────────────────────────────────                                                                                        
  void initGame() {
    level = 1;
    gameOver  = false;
    levelClear = false;                                                                                                                                                   
    pac.score = 0;
    pac.lives = 3;                                                                                                                                                        
    initLevel();  
  }

  void initLevel() {
    levelClear = false;
                                                                                                                                                                          
    pac.col      = COLS / 2;
    pac.row      = ROWS / 2;                                                                                                                                              
    pac.dir      = -1;                                                                                                                                                    
    pac.nextDir  = -1;
    pac.mouthOpen = true;                                                                                                                                                 
    pac.lastMove = millis();
    pac.moveDelay = max(80, 200 - (level - 1) * 20);                                                                                                                      
    pac.dead     = false;                                                                                                                                                 
    pac.deathTime = 0;                                                                                                                                                    
                                                                                                                                                                          
    spawnStars(); 
    spawnGhosts();
  }

  void spawnStars() {
    starsLeft = 0;
    int idx = 0;                                                                                                                                                          
    for (int r = 0; r < ROWS && idx < MAX_STARS; r++) {
      for (int c = 0; c < COLS && idx < MAX_STARS; c++) {                                                                                                                 
        if (abs(c - pac.col) < 2 && abs(r - pac.row) < 2) continue;
        if ((c + r) % 2 != 0) continue;                                                                                                                                   
        stars[idx].col       = c;                                                                                                                                         
        stars[idx].row       = r;                                                                                                                                         
        stars[idx].collected = false;                                                                                                                                     
        stars[idx].big       = (idx % 8 == 0);
        idx++;                                                                                                                                                            
      }
    }                                                                                                                                                                     
    starsLeft = idx;
    for (int i = idx; i < MAX_STARS; i++) stars[i].collected = true;
  }                                                                                                                                                                       
   
  void spawnGhosts() {                                                                                                                                                    
    uint16_t gcols[3] = { C_GHOST_R, C_GHOST_B, C_GHOST_P };
    int gstarts[3][2] = { {2, 2}, {COLS-3, 2}, {COLS/2, 3} };                                                                                                             
    for (int i = 0; i < MAX_GHOSTS; i++) {                                                                                                                                
      ghosts[i].col      = gstarts[i][0];                                                                                                                                 
      ghosts[i].row      = gstarts[i][1];                                                                                                                                 
      ghosts[i].dir      = i % 4;                                                                                                                                         
      ghosts[i].color    = gcols[i];
      ghosts[i].lastMove = millis() + i * 300;                                                                                                                            
      ghosts[i].active   = (i < min(3, 1 + (level - 1)));                                                                                                                 
    }                                                                                                                                                                     
  }                                                                                                                                                                       
                                                                                                                                                                          
  // ─────────────────────────────────────────────────────────────────────────────                                                                                        
  // TOUCH READ (FT3168 I2C)
  // ─────────────────────────────────────────────────────────────────────────────                                                                                        
  void readTouch() {
    Wire.beginTransmission(TOUCH_ADDR);                                                                                                                                   
    Wire.write(0x02);
    Wire.endTransmission(false);                                                                                                                                          
    Wire.requestFrom((uint8_t)TOUCH_ADDR, (uint8_t)6);  // FIX: explicit cast avoids wrong overload
                                                                                                                                                                          
    if (Wire.available() < 6) {                                                                                                                                           
      touched = false;                                                                                                                                                    
      touchX = -1; touchY = -1;                                                                                                                                           
      return;     
    }

    uint8_t td = Wire.read();
    uint8_t xh = Wire.read();
    uint8_t xl = Wire.read();
    uint8_t yh = Wire.read();
    uint8_t yl = Wire.read();
    Wire.read();

    if ((td & 0x0F) > 0) {                                                                                                                                                
      touchX = ((xh & 0x0F) << 8) | xl;
      touchY = ((yh & 0x0F) << 8) | yl;                                                                                                                                   
      touched = true;                                                                                                                                                     
    } else {
      touched = false;                                                                                                                                                    
      touchX = -1; touchY = -1;
    }
  }                                                                                                                                                                       
   
  // ─────────────────────────────────────────────────────────────────────────────                                                                                        
  // DPAD INPUT   
  // ─────────────────────────────────────────────────────────────────────────────
  void handleDpad() {                                                                                                                                                     
    if (!touched) return;
    for (int i = 0; i < 4; i++) {                                                                                                                                         
      if (touchX >= dpad[i].x && touchX < dpad[i].x + dpad[i].w &&
          touchY >= dpad[i].y && touchY < dpad[i].y + dpad[i].h) {
        pac.nextDir = dpad[i].dir;                                                                                                                                        
        return;
      }                                                                                                                                                                   
    }             
  }

  // ─────────────────────────────────────────────────────────────────────────────                                                                                        
  // GAME UPDATE
  // ─────────────────────────────────────────────────────────────────────────────                                                                                        
  void updateGame() {
    uint32_t now = millis();
                                                                                                                                                                          
    if (pac.dead) {
      if (now - pac.deathTime > 1000) {                                                                                                                                   
        pac.dead = false;
        pac.col = COLS / 2;                                                                                                                                               
        pac.row = ROWS / 2;
        pac.dir = -1;                                                                                                                                                     
        pac.nextDir = -1;
      }
      return;
    }

    movePac();                                                                                                                                                            
    moveGhosts();
    checkCollisions();                                                                                                                                                    
                  
    if (now % 300 < 150) pac.mouthOpen = true;                                                                                                                            
    else pac.mouthOpen = false;
                                                                                                                                                                          
    if (starsLeft <= 0) {
      levelClear = true;
      levelClearTime = now;
    }                                                                                                                                                                     
  }
                                                                                                                                                                          
  void movePac() {
    uint32_t now = millis();
    if (now - pac.lastMove < (uint32_t)pac.moveDelay) return;
    pac.lastMove = now;                                                                                                                                                   
   
    if (pac.nextDir >= 0) {                                                                                                                                               
      pac.dir = pac.nextDir;
      pac.nextDir = -1;
    }                                                                                                                                                                     
   
    if (pac.dir < 0) return;                                                                                                                                              
                  
    int nc = pac.col;
    int nr = pac.row;

    if (pac.dir == 0) nc++;
    else if (pac.dir == 1) nc--;
    else if (pac.dir == 2) nr--;
    else if (pac.dir == 3) nr++;
                                                                                                                                                                          
    if (nc < 0) nc = COLS - 1;
    if (nc >= COLS) nc = 0;                                                                                                                                               
    if (nr < 0) nr = ROWS - 1;
    if (nr >= ROWS) nr = 0;                                                                                                                                               
   
    pac.col = nc;                                                                                                                                                         
    pac.row = nr; 

    for (int i = 0; i < MAX_STARS; i++) {                                                                                                                                 
      if (!stars[i].collected && stars[i].col == pac.col && stars[i].row == pac.row) {
        stars[i].collected = true;                                                                                                                                        
        if (starsLeft > 0) starsLeft--;  // FIX: guard against going negative                                                                                             
        pac.score += stars[i].big ? 50 : 10;
      }                                                                                                                                                                   
    }             
  }                                                                                                                                                                       
                  
  void moveGhosts() {
    uint32_t now = millis();
    int dirs[4][2] = {{1,0},{-1,0},{0,-1},{0,1}};                                                                                                                         
                                                                                                                                                                          
    for (int i = 0; i < MAX_GHOSTS; i++) {                                                                                                                                
      if (!ghosts[i].active) continue;                                                                                                                                    
      uint32_t spd = max(100, (uint32_t)(GHOST_SPEED_MS - (level-1)*20));
      if (now - ghosts[i].lastMove < spd) continue;                                                                                                                       
      ghosts[i].lastMove = now;
                                                                                                                                                                          
      int d = ghosts[i].dir;                                                                                                                                              
      if (random(10) < 7) {
        int best = d;                                                                                                                                                     
        int bestDist = 9999;                                                                                                                                              
        for (int dd = 0; dd < 4; dd++) {
          int nc = ghosts[i].col + dirs[dd][0];                                                                                                                           
          int nr = ghosts[i].row + dirs[dd][1];
          if (nc < 0 || nc >= COLS || nr < 0 || nr >= ROWS) continue;                                                                                                     
          int dist = abs(nc - pac.col) + abs(nr - pac.row);
          if (dist < bestDist) { bestDist = dist; best = dd; }                                                                                                            
        }         
        d = best;                                                                                                                                                         
      } else {    
        int tries = 0;
        int reverse = (d < 2) ? (1 - d) : (5 - d);                                                                                                                        
        do {
          d = random(4);                                                                                                                                                  
          tries++;
        } while (d == reverse && tries < 5);
      }                                                                                                                                                                   
   
      int nc = ghosts[i].col + dirs[d][0];                                                                                                                                
      int nr = ghosts[i].row + dirs[d][1];
      if (nc >= 0 && nc < COLS && nr >= 0 && nr < ROWS) {
        ghosts[i].col = nc;                                                                                                                                               
        ghosts[i].row = nr;
        ghosts[i].dir = d;                                                                                                                                                
      }           
    }
  }

  void checkCollisions() {                                                                                                                                                
    for (int i = 0; i < MAX_GHOSTS; i++) {
      if (!ghosts[i].active) continue;                                                                                                                                    
      if (ghosts[i].col == pac.col && ghosts[i].row == pac.row) {
        pac.lives--;                                                                                                                                                      
        pac.dead = true;
        pac.deathTime = millis();                                                                                                                                         
        if (pac.lives <= 0) {
          gameOver = true;                                                                                                                                                
        }
        return;                                                                                                                                                           
      }           
    }
  }

  // ─────────────────────────────────────────────────────────────────────────────                                                                                        
  // RENDER
  // ─────────────────────────────────────────────────────────────────────────────                                                                                        
  void render() { 
    canvas->fillScreen(C_BG);
    drawWorld();
    drawStars();                                                                                                                                                          
    if (!pac.dead) drawPac();
    else drawDeathFlash();                                                                                                                                                
    drawGhosts();                                                                                                                                                         
    drawHUD();
    drawDpad();                                                                                                                                                           
    if (gameOver)    drawGameOver();
    if (levelClear)  drawLevelClear();
    canvas->flush();                                                                                                                                                      
  }
                                                                                                                                                                          
  void drawWorld() {
    for (int c = 0; c <= COLS; c++) {
      canvas->drawFastVLine(c * CELL, WORLD_Y, ROWS * CELL, C_GRID);
    }                                                                                                                                                                     
    for (int r = 0; r <= ROWS; r++) {
      canvas->drawFastHLine(0, WORLD_Y + r * CELL, COLS * CELL, C_GRID);                                                                                                  
    }                                                                                                                                                                     
  }
                                                                                                                                                                          
  void drawStars() {
    for (int i = 0; i < MAX_STARS; i++) {
      if (stars[i].collected) continue;                                                                                                                                   
      int cx = stars[i].col * CELL + CELL / 2;
      int cy = WORLD_Y + stars[i].row * CELL + CELL / 2;                                                                                                                  
      drawStar(cx, cy, stars[i].big);
    }                                                                                                                                                                     
  }               

  void drawStar(int cx, int cy, bool big) {                                                                                                                               
    if (big) {
      uint16_t col = (millis() / 200 % 2) ? C_STAR : 0xFD20;                                                                                                              
      canvas->fillCircle(cx, cy, 6, col);                                                                                                                                 
      canvas->drawLine(cx, cy-8, cx, cy+8, col);                                                                                                                          
      canvas->drawLine(cx-8, cy, cx+8, cy, col);                                                                                                                          
      canvas->drawLine(cx-6, cy-6, cx+6, cy+6, col);                                                                                                                      
      canvas->drawLine(cx+6, cy-6, cx-6, cy+6, col);                                                                                                                      
    } else {      
      canvas->fillCircle(cx, cy, 3, C_STAR);
    }
  }                                                                                                                                                                       
   
  void drawPac() {                                                                                                                                                        
    int cx = pac.col * CELL + CELL / 2;
    int cy = WORLD_Y + pac.row * CELL + CELL / 2;
    drawPacSprite(cx, cy, pac.dir, pac.mouthOpen);
  }

  void drawDeathFlash() {
    int cx = pac.col * CELL + CELL / 2;
    int cy = WORLD_Y + pac.row * CELL + CELL / 2;
    uint16_t col = (millis() / 100 % 2) ? 0xF800 : C_BG;                                                                                                                  
    canvas->drawLine(cx-7, cy-7, cx+7, cy+7, col);
    canvas->drawLine(cx+7, cy-7, cx-7, cy+7, col);                                                                                                                        
  }               

  void drawPacSprite(int cx, int cy, int dir, bool mouth) {
    int r = CELL / 2 - 1;
    canvas->fillCircle(cx, cy, r, C_PLAYER);                                                                                                                              
                                                                                                                                                                          
    if (mouth) {                                                                                                                                                          
      int mx = cx, my = cy;                                                                                                                                               
      int tip1x, tip1y, tip2x, tip2y;
      int mouthSize = r + 2;                                                                                                                                              
      if (dir == 0 || dir == -1) {                                                                                                                                        
        tip1x = cx + mouthSize; tip1y = cy - mouthSize/2;                                                                                                                 
        tip2x = cx + mouthSize; tip2y = cy + mouthSize/2;                                                                                                                 
      } else if (dir == 1) {
        tip1x = cx - mouthSize; tip1y = cy - mouthSize/2;                                                                                                                 
        tip2x = cx - mouthSize; tip2y = cy + mouthSize/2;
      } else if (dir == 2) {                                                                                                                                              
        tip1x = cx - mouthSize/2; tip1y = cy - mouthSize;
        tip2x = cx + mouthSize/2; tip2y = cy - mouthSize;                                                                                                                 
      } else {
        tip1x = cx - mouthSize/2; tip1y = cy + mouthSize;                                                                                                                 
        tip2x = cx + mouthSize/2; tip2y = cy + mouthSize;                                                                                                                 
      }
      canvas->fillTriangle(mx, my, tip1x, tip1y, tip2x, tip2y, C_BG);                                                                                                     
    }                                                                                                                                                                     
   
    int ex = cx, ey = cy - r/2;                                                                                                                                           
    if (dir == 0 || dir == -1) { ex = cx + 1; ey = cy - r/2 + 1; }
    else if (dir == 1) { ex = cx - 1; ey = cy - r/2 + 1; }                                                                                                                
    else if (dir == 2) { ex = cx;     ey = cy - r/2; }
    else               { ex = cx;     ey = cy + 1; }                                                                                                                      
    canvas->fillCircle(ex, ey, 2, C_PLAYER_EYE);
  }                                                                                                                                                                       
                  
  void drawGhosts() {                                                                                                                                                     
    for (int i = 0; i < MAX_GHOSTS; i++) {
      if (!ghosts[i].active) continue;                                                                                                                                    
      int cx = ghosts[i].col * CELL + CELL / 2;
      int cy = WORLD_Y + ghosts[i].row * CELL + CELL / 2;                                                                                                                 
      drawGhostSprite(cx, cy, ghosts[i].color);
    }                                                                                                                                                                     
  }
                                                                                                                                                                          
  void drawGhostSprite(int cx, int cy, uint16_t color) {
    int r = CELL / 2 - 1;
    canvas->fillCircle(cx, cy - 2, r, color);                                                                                                                             
    canvas->fillRect(cx - r, cy - 2, r * 2, r + 2, color);
    for (int wx = 0; wx < 3; wx++) {                                                                                                                                      
      canvas->fillCircle(cx - r + 2 + wx * (r * 2 / 3), cy + r, 3, C_BG);
    }                                                                                                                                                                     
    canvas->fillCircle(cx - 3, cy - 4, 3, C_WHITE);
    canvas->fillCircle(cx + 3, cy - 4, 3, C_WHITE);                                                                                                                       
    canvas->fillCircle(cx - 2, cy - 4, 1, 0x001F);
    canvas->fillCircle(cx + 4, cy - 4, 1, 0x001F);                                                                                                                        
  }                                                                                                                                                                       
                                                                                                                                                                          
  void drawHUD() {                                                                                                                                                        
    int hy = HUD_Y;
    canvas->fillRect(0, hy, LCD_W, 28, C_HUD_BG);
                                                                                                                                                                          
    canvas->setTextColor(C_SCORE);
    canvas->setTextSize(2);                                                                                                                                               
    canvas->setCursor(8, hy + 6);
    canvas->print("SCORE:");                                                                                                                                              
    canvas->print(pac.score);
                                                                                                                                                                          
    canvas->setTextColor(C_LIVES);                                                                                                                                        
    canvas->setCursor(LCD_W - 100, hy + 6);
    canvas->print("x");                                                                                                                                                   
    canvas->print(pac.lives);
    for (int l = 0; l < pac.lives && l < 3; l++) {                                                                                                                        
      int lx = LCD_W - 70 + l * 22;
      canvas->fillCircle(lx, hy + 14, 7, C_PLAYER);                                                                                                                       
      canvas->fillTriangle(lx, hy+14, lx+10, hy+8, lx+10, hy+20, C_BG);                                                                                                   
    }                                                                                                                                                                     
                                                                                                                                                                          
    canvas->setTextColor(C_WHITE);                                                                                                                                        
    canvas->setTextSize(1);
    canvas->setCursor(LCD_W/2 - 20, hy + 10);
    canvas->print("LVL ");                                                                                                                                                
    canvas->print(level);
                                                                                                                                                                          
    canvas->setCursor(LCD_W/2 - 20, hy + 20);                                                                                                                             
    canvas->setTextColor(C_STAR);
    canvas->print(starsLeft);                                                                                                                                             
    canvas->print(" left");
  }                                                                                                                                                                       
   
  void drawDpad() {                                                                                                                                                       
    const char* labels[4] = { ">", "<", "^", "v" };
                                                                                                                                                                          
    for (int i = 0; i < 4; i++) {
      bool pressing = touched &&                                                                                                                                          
        touchX >= dpad[i].x && touchX < dpad[i].x + dpad[i].w &&                                                                                                          
        touchY >= dpad[i].y && touchY < dpad[i].y + dpad[i].h;
                                                                                                                                                                          
      uint16_t bg = pressing ? C_DPAD_HI : C_DPAD;                                                                                                                        
                                                                                                                                                                          
      canvas->fillRoundRect(dpad[i].x, dpad[i].y, dpad[i].w, dpad[i].h, 10, bg);                                                                                          
      canvas->drawRoundRect(dpad[i].x, dpad[i].y, dpad[i].w, dpad[i].h, 10, C_WHITE);
                                                                                                                                                                          
      canvas->setTextColor(C_WHITE);                                                                                                                                      
      canvas->setTextSize(3);
      canvas->setCursor(dpad[i].x + dpad[i].w/2 - 9, dpad[i].y + dpad[i].h/2 - 12);                                                                                       
      canvas->print(labels[dpad[i].dir]);                                                                                                                                 
    }
  }                                                                                                                                                                       
                  
  void drawGameOver() {                                                                                                                                                   
    canvas->fillRoundRect(50, 160, LCD_W - 100, 180, 12, 0x1082);
    canvas->drawRoundRect(50, 160, LCD_W - 100, 180, 12, C_LIVES);                                                                                                        
                  
    canvas->setTextColor(C_LIVES);                                                                                                                                        
    canvas->setTextSize(3);
    canvas->setCursor(90, 180);                                                                                                                                           
    canvas->print("GAME OVER");
                                                                                                                                                                          
    canvas->setTextColor(C_SCORE);
    canvas->setTextSize(2);                                                                                                                                               
    canvas->setCursor(100, 230);
    canvas->print("SCORE: ");
    canvas->print(pac.score);                                                                                                                                             
   
    canvas->setTextColor(C_WHITE);                                                                                                                                        
    canvas->setTextSize(1);
    canvas->setCursor(110, 300);
    canvas->print("Tap to play again");                                                                                                                                   
  }
                                                                                                                                                                          
  void drawLevelClear() {
    canvas->fillRoundRect(60, 180, LCD_W - 120, 120, 12, 0x0840);
    canvas->drawRoundRect(60, 180, LCD_W - 120, 120, 12, C_STAR);                                                                                                         
                                                                                                                                                                          
    canvas->setTextColor(C_STAR);                                                                                                                                         
    canvas->setTextSize(2);                                                                                                                                               
    canvas->setCursor(85, 200);
    canvas->print("LEVEL CLEAR!");                                                                                                                                        
   
    canvas->setTextColor(C_WHITE);                                                                                                                                        
    canvas->setTextSize(2);
    canvas->setCursor(100, 240);
    canvas->print("SCORE: ");                                                                                                                                             
    canvas->print(pac.score);
  }