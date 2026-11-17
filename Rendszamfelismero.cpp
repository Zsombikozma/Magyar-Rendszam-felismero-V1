#include <opencv2/opencv.hpp>
#include <iostream>
#include <vector>
#include <algorithm>
#include <string>
#include <cmath>
#include <limits>

using namespace cv;
using namespace std;

// Egy karakter helye a rendszam kivagott kepen, es a binaris karakterkepe.
struct Karakter
{
    Rect hely;
    Mat kep;
};

// Egy lehetséges rendszam, a benne talalt karakterekkel.
struct RendszamJelolt
{
    Rect hely;
    vector<Karakter> karakterek;
    double pontszam = -1e100;
};

struct BetuMinta
{
    char betu;
    Mat kep;
};

// A megtalalt karaktert egységes meretre hozza.
Mat normalizal(const Mat& karakter)
{
    vector<Point> pontok;
    findNonZero(karakter, pontok);

    if (pontok.empty())
        return Mat::zeros(40, 25, CV_8UC1);

    Rect hatar = boundingRect(pontok);
    Mat eredmeny;
    resize(karakter(hatar), eredmeny, Size(25, 40), 0, 0, INTER_NEAREST);
    return eredmeny;
}

// Betu- es szammintakat keszit OpenCV-vel. Nem kell kulso OCR-konyvtar!
vector<BetuMinta> mintakKeszitese()
{
    vector<BetuMinta> mintak;
    string abece = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    int fontok[] = {
        FONT_HERSHEY_SIMPLEX,
        FONT_HERSHEY_DUPLEX,
        FONT_HERSHEY_COMPLEX,
        FONT_HERSHEY_TRIPLEX
    };
    double meretek[] = { 1.5, 2.0, 2.5 };
    int vastagsagok[] = { 2, 3, 4, 5 };

    for (char c : abece)
    {
        for (int font : fontok)
        {
            for (double meret : meretek)
            {
                for (int vastagsag : vastagsagok)
                {
                    Mat minta = Mat::zeros(110, 100, CV_8UC1);
                    putText(minta, string(1, c), Point(10, 82),
                        font, meret, Scalar(255), vastagsag, LINE_AA);
                    threshold(minta, minta, 127, 255, THRESH_BINARY);
                    mintak.push_back({ c, normalizal(minta) });
                }
            }
        }
    }
    return mintak;
}

// Egyetlen elkulonitett karakter felismerese mintakkal.
char karakterFelismerese(const Mat& karakter, const string& megengedett,
                        const vector<BetuMinta>& mintak)
{
    Mat vizsgalt = normalizal(karakter);
    int legkisebbHiba = numeric_limits<int>::max();
    char legjobb = '?';

    for (const BetuMinta& minta : mintak)
    {
        if (megengedett.find(minta.betu) == string::npos)
            continue;

        Mat kulonbseg;
        bitwise_xor(vizsgalt, minta.kep, kulonbseg);
        int hiba = countNonZero(kulonbseg);

        if (hiba < legkisebbHiba)
        {
            legkisebbHiba = hiba;
            legjobb = minta.betu;
        }
    }
    return legjobb;
}

// A rendszamjelolt belsejeben megkeresi a tenyleges betuket es szamokat.
vector<Karakter> karakterekKeresese(const Mat& rendszam)
{
    vector<Karakter> karakterek;
    int W = rendszam.cols;
    int H = rendszam.rows;
    if (W < 70 || H < 15)
        return karakterek;

    // 1. A bal oldali kek EU-sav felismerese, hogy ne legyen karakterjelolt.
    Mat hsv, kek;
    cvtColor(rendszam, hsv, COLOR_BGR2HSV);
    inRange(hsv, Scalar(95, 80, 35), Scalar(140, 255, 255), kek);

    int bal = static_cast<int>(W * 0.012);
    int oszlopLimit = static_cast<int>(W * 0.30);
    for (int x = 0; x < oszlopLimit; ++x)
    {
        if (countNonZero(kek.col(x)) > H * 0.22)
            bal = x + max(1, static_cast<int>(W * 0.01));
    }

    // 2. A rendszam felso/also keretet es az also reklamcsikot kihagyjuk.
    int fent = static_cast<int>(H * 0.045);
    int jobb = static_cast<int>(W * 0.985);
    int lent = static_cast<int>(H * 0.80);
    if (jobb <= bal || lent <= fent)
        return karakterek;

    Mat szurke, resz, szurt, binaris;
    cvtColor(rendszam, szurke, COLOR_BGR2GRAY);
    resz = szurke(Rect(bal, fent, jobb - bal, lent - fent));
    GaussianBlur(resz, szurt, Size(3, 3), 0);
    threshold(szurt, binaris, 0, 255, THRESH_BINARY_INV | THRESH_OTSU);

    // 3. Osszefuggo feher alakzatok = lehetseges sotet karakterek.
    Mat cimkek, statisztika, kozeppontok;
    int db = connectedComponentsWithStats(
        binaris, cimkek, statisztika, kozeppontok, 8, CV_32S);

    vector<Karakter> jeloltek;
    vector<int> magassagok;

    for (int i = 1; i < db; ++i) // 0. cimke a hatter
    {
        int x = statisztika.at<int>(i, CC_STAT_LEFT);
        int y = statisztika.at<int>(i, CC_STAT_TOP);
        int w = statisztika.at<int>(i, CC_STAT_WIDTH);
        int h = statisztika.at<int>(i, CC_STAT_HEIGHT);
        int terulet = statisztika.at<int>(i, CC_STAT_AREA);

        int kepX = x + bal;
        int kepY = y + fent;
        if (w > W * 0.012 && w < W * 0.23 &&
            h > H * 0.37 && h < H * 0.88 &&
            terulet > H * H * 0.009 &&
            kepX + w < W * 0.975)
        {
            jeloltek.push_back({ Rect(kepX, kepY, w, h),
                                 binaris(Rect(x, y, w, h)).clone() });
            magassagok.push_back(h);
        }
    }

    if (jeloltek.empty())
        return karakterek;

    // 4. A cimer es egyeb kisebb reszletek kiszurese magassag alapjan.
    sort(magassagok.begin(), magassagok.end());
    int n = static_cast<int>(magassagok.size());
    double medianMagassag = (n % 2 != 0)
        ? static_cast<double>(magassagok[n / 2])
        : (magassagok[n / 2 - 1] + magassagok[n / 2]) / 2.0;

    for (const Karakter& k : jeloltek)
    {
        if (k.hely.height >= 0.81 * medianMagassag &&
            k.hely.height <= 1.30 * medianMagassag)
            karakterek.push_back(k);
    }

    // 5. Balrol jobbra olvasunk.
    sort(karakterek.begin(), karakterek.end(),
        [](const Karakter& a, const Karakter& b)
        {
            return a.hely.x < b.hely.x;
        });

    return karakterek;
}

// Az E G E S Z autokepen keresunk, nem a hutorarcsnal valasztunk ROI-t.
RendszamJelolt rendszamKeresese(const Mat& kep)
{
    int W = kep.cols;
    int H = kep.rows;

    Mat hsv, szurke;
    cvtColor(kep, hsv, COLOR_BGR2HSV);
    cvtColor(kep, szurke, COLOR_BGR2GRAY);

    // Kulonbozo megvilagitasokhoz tobb kulonbozo vilagosmaszk.
    Mat mask1, mask2, mask3;
    inRange(hsv, Scalar(0, 0, 170), Scalar(179, 120, 255), mask1);
    inRange(hsv, Scalar(0, 0, 200), Scalar(179, 130, 255), mask2);
    threshold(szurke, mask3, 170, 255, THRESH_BINARY);

    vector<Mat> maszkok = { mask1, mask2, mask3 };
    RendszamJelolt legjobb;

    int kw = max(3, static_cast<int>(W * 0.014));
    int kh = max(3, static_cast<int>(H * 0.009));
    Mat kernel = getStructuringElement(MORPH_RECT, Size(kw, kh));

    for (Mat& maszk : maszkok)
    {
        Mat zart;
        morphologyEx(maszk, zart, MORPH_CLOSE, kernel);

        vector<vector<Point>> konturok;
        findContours(zart, konturok, RETR_EXTERNAL, CHAIN_APPROX_SIMPLE);

        for (const vector<Point>& kontur : konturok)
        {
            Rect t = boundingRect(kontur);
            double arany = static_cast<double>(t.width) / t.height;
            double kozepX = (t.x + t.width / 2.0) / W;
            double kozepY = (t.y + t.height / 2.0) / H;

            if (!(arany > 2.9 && arany < 6.8 &&
                  t.width > W * 0.07 && t.width < W * 0.78 &&
                  t.height > H * 0.025 &&
                  kozepX > 0.20 && kozepX < 0.82 &&
                  kozepY > 0.26 && kozepY < 0.96))
                continue;

            double vilagosArany = static_cast<double>(
                countNonZero(zart(t))) / t.area();

            if (vilagosArany < 0.12)
                continue;

            vector<Karakter> karakterek = karakterekKeresese(kep(t));
            int db = static_cast<int>(karakterek.size());
            if (db == 0)
                continue;

            // Csak a karakterek szamaval NEM eleg donteni:
            // a sor egyenletesseget es a feher hatteret is pontozzuk.
            double atlag = 0.0;
            for (const Karakter& k : karakterek)
                atlag += k.hely.height;
            atlag /= db;

            double szoras2 = 0.0;
            for (const Karakter& k : karakterek)
            {
                double kul = k.hely.height - atlag;
                szoras2 += kul * kul;
            }
            szoras2 /= db;
            double relativSzoras = sqrt(szoras2) / atlag;

            double karakterSorSzelesseg =
                (karakterek.back().hely.x + karakterek.back().hely.width
                 - karakterek.front().hely.x) / static_cast<double>(t.width);

            double pontszam =
                ((db == 6 || db == 7) ? 100.0 : 10.0 * min(db, 8))
                - 12.0 * relativSzoras
                + 20.0 * vilagosArany
                + 3.0 * log(1.0 + t.area())
                + 20.0 * karakterSorSzelesseg;

            if (pontszam > legjobb.pontszam)
            {
                legjobb.hely = t;
                legjobb.karakterek = karakterek;
                legjobb.pontszam = pontszam;
            }
        }
    }

    return legjobb;
}

int main(int argc, char* argv[])
{
    // Alapbol a munkakonyvtar "kepek" almappajaban keresunk.
    // Masik mappa a program elso parancssori argumentumakent megadhato.
    const string mappa = (argc > 1) ? argv[1] : "kepek";
    string utvonal = mappa;
    if (!utvonal.empty() && utvonal.back() != '/' && utvonal.back() != '\\')
        utvonal += "/";

    // Mind a hat tesztkepet feldolgozza, egymas utan.
    const vector<string> fajlok = {
        "auto2.jpg", "auto4.jpg", "auto5.jpg",
        "auto9.jpg", "auto10.jfif", "auto11.jpg"
    };

    cout << "OpenCV mintakarakterek elokeszitese..." << endl;
    vector<BetuMinta> mintak = mintakKeszitese();
    cout << "Mintak szama: " << mintak.size() << endl << endl;

    namedWindow("Rendszam helye", WINDOW_NORMAL);
    namedWindow("Karakterek", WINDOW_NORMAL);
    resizeWindow("Rendszam helye", 950, 600);
    resizeWindow("Karakterek", 900, 250);

    for (const string& fajl : fajlok)
    {
        Mat kep = imread(utvonal + fajl);
        if (kep.empty())
        {
            cout << fajl << ": Nem sikerult megnyitni!" << endl;
            continue;
        }

        RendszamJelolt rendszam = rendszamKeresese(kep);
        if (rendszam.pontszam < -1e50)
        {
            cout << fajl << ": Nem talaltam rendszamjeloltet." << endl;
            continue;
        }

        Mat jeloltKep = kep.clone();
        rectangle(jeloltKep, rendszam.hely, Scalar(0, 255, 0), 3);
        Mat kivagott = kep(rendszam.hely).clone();

        for (const Karakter& karakter : rendszam.karakterek)
            rectangle(kivagott, karakter.hely, Scalar(0, 255, 0), 1);

        string szoveg = "Nem sikerult felismerni";
        int db = static_cast<int>(rendszam.karakterek.size());

        if (db == 6 || db == 7)
        {
            string karakterSor;
            for (int i = 0; i < db; ++i)
            {
                string lehetseges = (i < db - 3)
                    ? "ABCDEFGHIJKLMNOPQRSTUVWXYZ" : "0123456789";
                karakterSor += karakterFelismerese(
                    rendszam.karakterek[i].kep, lehetseges, mintak);
            }

            if (db == 6)
                szoveg = karakterSor.substr(0, 3) + "-" + karakterSor.substr(3);
            else
                szoveg = karakterSor.substr(0, 2) + " " +
                         karakterSor.substr(2, 2) + "-" + karakterSor.substr(4);
        }

        cout << fajl << " | " << db << " karakter | " << szoveg << endl;

        putText(jeloltKep, szoveg,
            Point(rendszam.hely.x, max(30, rendszam.hely.y - 10)),
            FONT_HERSHEY_SIMPLEX, 0.85, Scalar(0, 255, 0), 2, LINE_AA);

        Mat nagyitott;
        resize(kivagott, nagyitott, Size(), 3.0, 3.0, INTER_LINEAR);
        imshow("Rendszam helye", jeloltKep);
        imshow("Karakterek", nagyitott);

        // Minden kep eredmenyet elmenti a kepek mappaba.
        string rovidNev = fajl.substr(0, fajl.find_last_of('.'));
        imwrite(utvonal + "eredmeny_" + rovidNev + ".png", jeloltKep);

        cout << "Nyomj meg egy billentyut a kovetkezo kephez (Esc = vege)!" << endl;
        int billentyu = waitKey(0);
        if (billentyu == 27)
            break;
    }

    destroyAllWindows();
    return 0;
}
